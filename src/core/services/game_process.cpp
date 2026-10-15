#include "core/services/game_process.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include <QCoro/QCoroTask>
#include <QCoro/QCoroTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <spdlog/spdlog.h>

#include "core/constants.h"
#include "core/file_io.h"
#include "core/providers/base.h"

#if defined(Q_OS_LINUX)
#include <dirent.h>
#elif defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <tlhelp32.h>
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/types.h>
#include <vector>
#endif

namespace px::core::services {

namespace {

const QSet<QString> kProcessNames = {
    QStringLiteral("RimWorld"),
    QStringLiteral("RimWorldLinux"),
    QStringLiteral("RimWorldWin64.exe"),
    QStringLiteral("RimWorldWin.exe"),
    QStringLiteral("RimWorld.exe"),
    QStringLiteral("RimWorld by Ludeon Studios"),
};

// Simple POSIX shlex-style tokenizer. Handles single/double quotes and backslash escapes.
Result<QStringList> split_posix(const QString& text) {
    QStringList tokens;
    QString current;
    enum State { Space, Word, SingleQuote, DoubleQuote } state = Space;
    bool escaped = false;

    for (const QChar ch : text) {
        if (escaped) {
            if (state == DoubleQuote && ch != u'"' && ch != u'\\' && ch != u'$' && ch != u'`') {
                current.append(u'\\');
            }
            current.append(ch);
            escaped = false;
            continue;
        }

        switch (state) {
        case Space:
            if (ch.isSpace()) {
                continue;
            }
            if (ch == u'\\') {
                escaped = true;
                state = Word;
            } else if (ch == u'\'') {
                state = SingleQuote;
            } else if (ch == u'"') {
                state = DoubleQuote;
            } else {
                current.append(ch);
                state = Word;
            }
            break;

        case Word:
            if (ch.isSpace()) {
                tokens.append(current);
                current.clear();
                state = Space;
            } else if (ch == u'\\') {
                escaped = true;
            } else if (ch == u'\'') {
                state = SingleQuote;
            } else if (ch == u'"') {
                state = DoubleQuote;
            } else {
                current.append(ch);
            }
            break;

        case SingleQuote:
            if (ch == u'\'') {
                state = Word;
            } else {
                current.append(ch);
            }
            break;

        case DoubleQuote:
            if (ch == u'\\') {
                escaped = true;
            } else if (ch == u'"') {
                state = Word;
            } else {
                current.append(ch);
            }
            break;
        }
    }

    if (escaped || state == SingleQuote || state == DoubleQuote) {
        return std::unexpected(Error{QStringLiteral("No closing quotation")});
    }
    if (state == Word || !current.isEmpty()) {
        tokens.append(current);
    }
    return tokens;
}

// Python's shlex.split(posix=False), which the Windows launch path uses: quotes are kept in the
// token and there are no escapes, so whitespace inside quotes does not split.
Result<QStringList> split_windows(const QString& text) {
    QStringList tokens;
    QString current;
    bool inToken = false;
    QChar quote;
    for (const QChar ch : text) {
        if (!quote.isNull()) {
            current.append(ch);
            if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == u'"' || ch == u'\'') {
            quote = ch;
            current.append(ch);
            inToken = true;
        } else if (ch.isSpace()) {
            if (inToken) {
                tokens.append(current);
                current.clear();
                inToken = false;
            }
        } else {
            current.append(ch);
            inToken = true;
        }
    }
    if (!quote.isNull()) {
        return std::unexpected(Error{QStringLiteral("No closing quotation")});
    }
    if (inToken) {
        tokens.append(current);
    }
    return tokens;
}

bool is_env_assignment(const QString& token) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    return pattern.match(token).hasMatch();
}

}  // namespace

Platform current_platform() {
#if defined(Q_OS_MACOS)
    return Platform::MacOS;
#elif defined(Q_OS_WIN)
    return Platform::Windows;
#else
    return Platform::Linux;
#endif
}

Result<QStringList> split_arguments(const QString& text, Platform platform) {
    return platform == Platform::Windows ? split_windows(text) : split_posix(text);
}

Result<DirectCommand> build_direct_command(const QString& exe, const QString& wrapper,
                                           const QString& args, Platform platform) {
    auto splitArgs = split_arguments(args, platform);
    if (!splitArgs) {
        return std::unexpected(splitArgs.error());
    }
    auto splitWrapper = split_arguments(wrapper, platform);
    if (!splitWrapper) {
        return std::unexpected(splitWrapper.error());
    }

    QStringList game;
    game.append(exe);
    game.append(*splitArgs);

    QStringList tokens = *splitWrapper;
    DirectCommand result;
    while (!tokens.isEmpty() && is_env_assignment(tokens.front())) {
        const QString token = tokens.takeFirst();
        const qsizetype eq = token.indexOf(u'=');
        result.env.insert(token.left(eq), token.mid(eq + 1));
    }

    const qsizetype placeholder = tokens.indexOf(QStringLiteral("%command%"));
    if (placeholder >= 0) {
        result.argv = tokens.mid(0, placeholder);
        result.argv.append(game);
        result.argv.append(tokens.mid(placeholder + 1));
    } else {
        result.argv = tokens;
        result.argv.append(game);
    }
    return result;
}

std::optional<QString> find_executable(const QString& game_path, Platform platform) {
    const QDir dir(game_path);
    if (platform == Platform::Linux) {
        for (const char* name : {"RimWorld", "RimWorldLinux"}) {
            const QString candidate = dir.filePath(QString::fromLatin1(name));
            const QFileInfo info(candidate);
            if (info.isFile() && info.isExecutable()) {
                return candidate;
            }
        }
        return std::nullopt;
    }
    if (platform == Platform::MacOS) {
        const QString primary = dir.filePath(QStringLiteral("RimWorld.app"));
        if (QFileInfo(primary).isDir()) {
            return primary;
        }
        QStringList apps = dir.entryList(QStringList{QStringLiteral("*.app")}, QDir::Dirs);
        apps.sort();
        if (!apps.isEmpty()) {
            return dir.filePath(apps.first());
        }
        return std::nullopt;
    }
    for (const char* name : {"RimWorld.exe", "RimWorldWin64.exe", "RimWorldWin.exe"}) {
        const QString candidate = dir.filePath(QString::fromLatin1(name));
        if (QFileInfo(candidate).isFile()) {
            return candidate;
        }
    }
    return std::nullopt;
}

void ensure_steam_appid(const QString& game_path, Platform platform) {
    const QString targetDir =
        platform == Platform::MacOS ? QFileInfo(game_path).dir().absolutePath() : game_path;
    const QString path = QDir(targetDir).filePath(QStringLiteral("steam_appid.txt"));
    if (QFileInfo::exists(path)) {
        return;
    }
    const std::string_view appId(RIMWORLD_STEAM_APP_ID.data(), RIMWORLD_STEAM_APP_ID.size());
    if (auto written = write_file_atomically(path, appId); !written) {
        spdlog::warn("Failed to create steam_appid.txt: {}", written.error().message.toStdString());
    } else {
        spdlog::debug("Created {}", path.toStdString());
    }
}

QString steam_run_url(const QString& launch_args) {
    const QString trimmed = launch_args.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("steam://rungameid/%1").arg(QString(RIMWORLD_STEAM_APP_ID));
    }
    const QByteArray encoded = QUrl::toPercentEncoding(trimmed);
    return QStringLiteral("steam://run/%1//%2/")
        .arg(QString(RIMWORLD_STEAM_APP_ID), QString::fromUtf8(encoded));
}

Result<void> open_steam_url(const QString& launch_args) {
    const QString url = steam_run_url(launch_args);
    spdlog::info("Opening Steam URL: {}", url.toStdString());
#if defined(Q_OS_WIN)
    const bool started = QProcess::startDetached(
        QStringLiteral("cmd"),
        {QStringLiteral("/c"), QStringLiteral("start"), QString(), url});
#elif defined(Q_OS_MACOS)
    const bool started = QProcess::startDetached(QStringLiteral("open"), {url});
#else
    const bool started = QProcess::startDetached(QStringLiteral("xdg-open"), {url});
#endif
    if (!started) {
        return std::unexpected(Error{QStringLiteral("could not start the system URL opener")});
    }
    return {};
}

std::vector<ProcessInfo> host_process_lister() {
    std::vector<ProcessInfo> procs;
#if defined(Q_OS_LINUX)
    DIR* proc = opendir("/proc");
    if (!proc) {
        return procs;
    }
    while (struct dirent* entry = readdir(proc)) {
        if (entry->d_type != DT_DIR) {
            continue;
        }
        bool ok = false;
        const qint64 pid = QString::fromLatin1(entry->d_name).toLongLong(&ok);
        if (!ok || pid <= 0) {
            continue;
        }
        QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray stat = file.readAll();
        const qsizetype openParen = stat.indexOf('(');
        const qsizetype closeParen = stat.lastIndexOf(')');
        if (openParen < 0 || closeParen <= openParen) {
            continue;
        }
        const QString comm = QString::fromUtf8(stat.mid(openParen + 1, closeParen - openParen - 1));
        const QByteArray rest = stat.mid(closeParen + 2);
        const bool zombie = !rest.isEmpty() && rest.at(0) == 'Z';
        procs.push_back(ProcessInfo{pid, comm, zombie});
    }
    closedir(proc);
#elif defined(Q_OS_WIN)
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return procs;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        // The toolhelp snapshot does not expose process state, so zombies are never seen.
        procs.push_back(ProcessInfo{static_cast<qint64>(entry.th32ProcessID),
                                    QString::fromWCharArray(entry.szExeFile), false});
    }
    CloseHandle(snapshot);
#elif defined(Q_OS_MACOS)
    // proc_listallpids takes a buffer size in bytes and returns the number of pids written.
    std::vector<pid_t> pids(static_cast<std::size_t>(proc_listallpids(nullptr, 0)) + 64);
    const int written = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    // SZOMBIE from <sys/proc.h>, which is not usable from user code.
    constexpr std::uint32_t kZombieStatus = 5;
    for (int i = 0; i < written; ++i) {
        const pid_t pid = pids[static_cast<std::size_t>(i)];
        char name[256] = {};
        if (proc_name(pid, name, sizeof(name)) <= 0) {
            continue;
        }
        proc_bsdinfo info{};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info))) {
            continue;
        }
        procs.push_back(ProcessInfo{pid, QString::fromUtf8(name), info.pbi_status == kZombieStatus});
    }
#endif
    return procs;
}

QSet<qint64> live_game_pids(const ProcessLister& lister) {
    QSet<qint64> pids;
    for (const ProcessInfo& proc : lister()) {
        if (!proc.zombie && kProcessNames.contains(proc.name)) {
            pids.insert(proc.pid);
        }
    }
    return pids;
}

bool is_game_running(const ProcessLister& lister) {
    return !live_game_pids(lister).isEmpty();
}

GameProcessMonitor::GameProcessMonitor(ProcessLister lister, MonitorTiming timing)
    : lister_(std::move(lister)), timing_(timing) {}

GameProcessMonitor::~GameProcessMonitor() {
    drop_process();
}

QCoro::Task<void> GameProcessMonitor::capture_baseline() {
    baseline_ = co_await providers::runOnPool<QSet<qint64>>(nullptr, [this] {
        return live_game_pids(lister_);
    });
}

void GameProcessMonitor::attach_process(std::unique_ptr<QProcess> proc) {
    drop_process();
    proc_ = std::move(proc);
}

bool GameProcessMonitor::launched_alive() const {
    const QSet<qint64> live = live_game_pids(lister_);
    return !(live - baseline_).isEmpty();
}

void GameProcessMonitor::drop_process() {
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        // Intentionally leaked: the child keeps running after the monitor is gone, as it did
        // under Popen, and destroying a running QProcess would kill it.
        static_cast<void>(proc_.release());
        return;
    }
    proc_.reset();
}

QCoro::Task<bool> GameProcessMonitor::wait_for_start(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const bool alive = co_await providers::runOnPool<bool>(nullptr, [this] {
            return launched_alive();
        });
        if (alive) {
            co_return true;
        }
        if (proc_ && proc_->state() == QProcess::NotRunning) {
            co_return false;
        }
        if (timing_.poll_start.count() > 0) {
            co_await QCoro::sleepFor(timing_.poll_start);
        }
    }
    co_return false;
}

QCoro::Task<std::optional<int>> GameProcessMonitor::wait_for_exit() {
    while (co_await providers::runOnPool<bool>(nullptr, [this] { return launched_alive(); })) {
        if (timing_.poll_exit.count() > 0) {
            co_await QCoro::sleepFor(timing_.poll_exit);
        }
    }
    if (!proc_) {
        co_return std::nullopt;
    }
    std::optional<int> code;
    if (proc_->state() == QProcess::NotRunning) {
        code = proc_->exitCode();
    }
    drop_process();
    co_return code;
}

}  // namespace px::core::services
