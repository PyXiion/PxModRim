#pragma once
#include <QString>
namespace px::modrim {
QString escape_html(const QString& text);
QString unity_rich_text_to_html(const QString& text);
}
