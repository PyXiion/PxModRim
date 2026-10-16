#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QCoro/QCoroTask>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/downloads/downloader.h"
#include "core/downloads/manager.h"
#include "core/downloads/types.h"
#include "core/models/metadata/structures.h"
#include "../test_support.h"

namespace {

using px::modrim::Downloader;
using px::modrim::DownloadManager;
using px::modrim::DownloadResult;
using px::modrim::ListedMod;
using px::modrim::ListedModPtr;
using px::modrim::Result;
using test_support::run_sync;

// Claims ids that start with `prefix`, and records each batch it was asked to download.
class FakeDownloader : public Downloader {
public:
    FakeDownloader(QString name, QString label, QString prefix, DownloadManager& manager)
        : Downloader(std::move(name), std::move(label), manager), prefix_(std::move(prefix)) {}

    bool is_downloading() const override { return busy_; }
    QSet<QString> active_ids() const override { return {}; }
    bool accepts(const QString& mod_id) const override { return mod_id.startsWith(prefix_); }

    std::optional<QString> updatable_id(const ListedMod& mod) const override {
        const auto& path = mod.mod_path();
        if (!path) {
            return std::nullopt;
        }
        const QString id = QFileInfo(*path).fileName();
        return accepts(id) ? std::optional<QString>(id) : std::nullopt;
    }

    std::optional<double> last_synced(const ListedMod& mod) const override {
        return updatable_id(mod) ? std::optional<double>(1.0) : std::nullopt;
    }

    QCoro::Task<Result<DownloadResult>> download_mods(QStringList mod_ids) override {
        calls.push_back(mod_ids);
        busy_ = true;
        busy_changed.notify(true);
        busy_ = false;
        busy_changed.notify(false);
        DownloadResult result{.succeeded = mod_ids, .failed = {}, .changed = {}};
        download_finished.notify(result);
        co_return result;
    }

    void cancel() override { cancelled = true; }

    std::vector<QStringList> calls;
    bool cancelled = false;

private:
    QString prefix_;
    bool busy_ = false;
};

class FakeSteam : public FakeDownloader {
public:
    explicit FakeSteam(DownloadManager& manager) : FakeDownloader("steam", "Steam", "s", manager) {}
};

class FakeGithub : public FakeDownloader {
public:
    explicit FakeGithub(DownloadManager& manager) : FakeDownloader("github", "Github", "g", manager) {}
};

ListedModPtr make_mod(const QString& folder) {
    auto mod = std::make_shared<ListedMod>();
    REQUIRE(mod->set_mod_path(QStringLiteral("/mods/%1").arg(folder)).has_value());
    return mod;
}

}  // namespace

TEST_CASE("empty manager offers nothing", "[downloads][manager]") {
    DownloadManager manager;
    REQUIRE_FALSE(manager.available());
    REQUIRE(manager.updatable_ids({make_mod("s1")}).isEmpty());
    REQUIRE(manager.source_of(*make_mod("s1")) == nullptr);
}

TEST_CASE("download routes ids to the accepting downloader", "[downloads][manager]") {
    DownloadManager manager;
    FakeSteam steam(manager);
    FakeGithub github(manager);
    manager.register_downloader(steam);
    manager.register_downloader(github);

    auto result = run_sync(manager.download_mods({"s1", "g1", "s2", "s1"}));

    REQUIRE(result.has_value());
    REQUIRE(steam.calls == std::vector<QStringList>{{"s1", "s2"}});
    REQUIRE(github.calls == std::vector<QStringList>{{"g1"}});
    QStringList succeeded = result->succeeded;
    succeeded.sort();
    REQUIRE(succeeded == QStringList{"g1", "s1", "s2"});
}

TEST_CASE("unclaimed id is rejected before anything starts", "[downloads][manager]") {
    DownloadManager manager;
    FakeSteam steam(manager);
    manager.register_downloader(steam);

    auto result = run_sync(manager.download_mods({"s1", "x1"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.contains("x1"));
    REQUIRE(steam.calls.empty());
}

TEST_CASE("source and ids come from the owning downloader", "[downloads][manager]") {
    DownloadManager manager;
    FakeSteam steam(manager);
    FakeGithub github(manager);
    manager.register_downloader(steam);
    manager.register_downloader(github);
    const auto g1 = make_mod("g1");
    const auto other = make_mod("other");
    const QList<ListedModPtr> mods{make_mod("s1"), g1, other};

    const Downloader* source = manager.source_of(*g1);
    REQUIRE(source != nullptr);
    REQUIRE(source->label() == "Github");
    REQUIRE(manager.updatable_ids(mods) == QStringList{"s1", "g1"});
    REQUIRE_FALSE(manager.last_synced(*other).has_value());
}

TEST_CASE("busy reflects any downloader and cancel stops the rest", "[downloads][manager]") {
    DownloadManager manager;
    FakeSteam steam(manager);
    FakeGithub github(manager);
    manager.register_downloader(steam);
    manager.register_downloader(github);
    std::vector<bool> busy;
    manager.busy_changed.connect([&busy](const bool& value) { busy.push_back(value); });

    steam.download_finished.connect([&manager](const DownloadResult&) { manager.cancel(); });
    auto result = run_sync(manager.download_mods({"s1", "g1"}));

    REQUIRE(result.has_value());
    REQUIRE(github.calls.empty());
    REQUIRE(busy == std::vector<bool>{true, false});
}

TEST_CASE("names are keyed by source id, not workshop id", "[downloads][manager]") {
    DownloadManager manager;
    FakeGithub github(manager);
    manager.register_downloader(github);
    const auto mod = make_mod("g1");
    mod->name = QStringLiteral("Git Mod");

    const QHash<QString, QString> names = manager.names_by_id({mod, make_mod("s1")});

    REQUIRE(names == QHash<QString, QString>{{"g1", "Git Mod"}});
}
