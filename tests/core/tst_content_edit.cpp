#include "core/oiv/content_edit.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestContentEdit : public QObject {
    Q_OBJECT
private slots:
    void officialTextSequence();
    void lfFileStaysLf();
    void emptyFileTakesAddedLines();
    void addFirstLastBeforeAfter();
    void replaceAndRemove();
    void relativeXPathAndDefaultAppend();
    void badXPathIsError();
    void addToNothingIsError();
    void entitiesAreNotExpanded();
};

namespace {

OivText text(OivText::Kind kind, std::string value, std::string line = {}, OivText::Condition condition = OivText::Condition::Equal, bool after = false)
{
    return { kind, std::move(value), std::move(line), condition, after };
}

OivXml xml(OivXml::Kind kind, std::string xpath, std::string fragment = {}, OivXml::Append append = OivXml::Append::Last)
{
    return { kind, std::move(xpath), append, std::move(fragment) };
}

const std::string dlclist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SMandatoryPacksData>\n\t<Paths>\n\t\t<Item>dlcpacks:/mpheist/</Item>\n\t\t<Item>dlcpacks:/patchday21ng/</Item>\n\t\t<Item>dlcpacks:/last/</Item>\n\t</Paths>\n</SMandatoryPacksData>\n";

std::string edited(const std::vector<OivXml> &edits, const std::string &source = dlclist)
{
    const auto result = applyXmlEdits(toVector(source), edits);
    return result ? toText(*result) : "<error: " + result.error().message + ">";
}

}

void TestContentEdit::officialTextSequence()
{
    const std::string original = "Line 1\r\nLine 2\r\n# THIS LINE\r\nLine 3\r\nLine 4\r\nLine 5\r\n#####\r\n#####";
    const std::vector<OivText> edits {
        text(OivText::Kind::Add, "This line is added"),
        text(OivText::Kind::Delete, {}, "Line 3"),
        text(OivText::Kind::Replace, "THIS IS NEW LINE", "Line 4", OivText::Condition::StartWith),
        text(OivText::Kind::Insert, "This is last line", "Line 5", OivText::Condition::Equal, true),
        text(OivText::Kind::Insert, "This is first line", "Line 1", OivText::Condition::Equal, false),
        text(OivText::Kind::Delete, {}, "#*", OivText::Condition::Mask),
    };
    QCOMPARE(toText(applyTextEdits(toVector(original), edits)),
        std::string("This is first line\r\nLine 1\r\nLine 2\r\nTHIS IS NEW LINE\r\nLine 5\r\nThis is last line\r\nThis line is added"));
}

void TestContentEdit::lfFileStaysLf()
{
    const std::vector<OivText> edits {
        text(OivText::Kind::Replace, "new", "OLD*", OivText::Condition::Mask),
        text(OivText::Kind::Insert, "after first", "old one", OivText::Condition::Equal, true),
        text(OivText::Kind::Delete, {}, "Missing line"),
    };
    QCOMPARE(toText(applyTextEdits(toVector("old one\nkeep\nold two\n"), edits)), std::string("new\nkeep\nnew\n"));
    QCOMPARE(toText(applyTextEdits(toVector("a\nb\n"), std::vector<OivText> { text(OivText::Kind::Insert, "x", "a", OivText::Condition::Equal, true) })), std::string("a\nx\nb\n"));
}

void TestContentEdit::emptyFileTakesAddedLines()
{
    const std::vector<OivText> edits { text(OivText::Kind::Add, "one"), text(OivText::Kind::Add, "two") };
    QCOMPARE(toText(applyTextEdits({}, edits)), std::string("one\r\ntwo"));
}

void TestContentEdit::addFirstLastBeforeAfter()
{
    const std::string result = edited({
        xml(OivXml::Kind::Add, "/SMandatoryPacksData/Paths", "<Item>first</Item>", OivXml::Append::First),
        xml(OivXml::Kind::Add, "/SMandatoryPacksData/Paths", "<Item>last</Item><Item>last2</Item>", OivXml::Append::Last),
        xml(OivXml::Kind::Add, "/SMandatoryPacksData/Paths/Item[.='dlcpacks:/mpheist/']", "<Item>before</Item>", OivXml::Append::Before),
        xml(OivXml::Kind::Add, "/SMandatoryPacksData/Paths/Item[.='dlcpacks:/patchday21ng/']", "<Item>after</Item><Item>after2</Item>", OivXml::Append::After),
    });
    QCOMPARE(result, std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SMandatoryPacksData>\n\t<Paths>\n\t\t<Item>first</Item>\n\t\t<Item>before</Item>\n\t\t<Item>dlcpacks:/mpheist/</Item>\n\t\t<Item>dlcpacks:/patchday21ng/</Item>\n\t\t<Item>after</Item>\n\t\t<Item>after2</Item>\n\t\t<Item>dlcpacks:/last/</Item>\n\t\t<Item>last</Item>\n\t\t<Item>last2</Item>\n\t</Paths>\n</SMandatoryPacksData>\n"));
}

void TestContentEdit::replaceAndRemove()
{
    const std::string handling = "<CHandlingDataMgr><HandlingData><Item type=\"CHandlingData\"><handlingName>BUFFALO</handlingName><fMass value=\"1.0\"/></Item><Item type=\"CHandlingData\"><handlingName>ADDER</handlingName><fMass value=\"2.0\"/></Item></HandlingData></CHandlingDataMgr>";
    const std::string result = edited({
        xml(OivXml::Kind::Replace, "/CHandlingDataMgr/HandlingData/Item[@type=\"CHandlingData\"]/handlingName[.=\"BUFFALO\"]/../fMass", "<fMass value=\"999999.000\"/>"),
        xml(OivXml::Kind::Remove, "//Item[handlingName=\"ADDER\"]"),
    }, handling);
    QCOMPARE(result, std::string("<CHandlingDataMgr>\n\t<HandlingData>\n\t\t<Item type=\"CHandlingData\">\n\t\t\t<handlingName>BUFFALO</handlingName>\n\t\t\t<fMass value=\"999999.000\" />\n\t\t</Item>\n\t</HandlingData>\n</CHandlingDataMgr>\n"));
    QCOMPARE(edited({ xml(OivXml::Kind::Remove, "/SMandatoryPacksData/Paths/Item[15]") }), edited({}));
}

void TestContentEdit::relativeXPathAndDefaultAppend()
{
    const std::string result = edited({ xml(OivXml::Kind::Add, "SMandatoryPacksData/Paths", "<item>dlcpacks:\\r8ppi\\</item>") });
    QVERIFY(result.find("<Item>dlcpacks:/last/</Item>\n\t\t<item>dlcpacks:\\r8ppi\\</item>\n\t</Paths>") != std::string::npos);
}

void TestContentEdit::badXPathIsError()
{
    QVERIFY(!applyXmlEdits(toVector(dlclist), std::vector<OivXml> { xml(OivXml::Kind::Remove, "/a[[") }));
    QVERIFY(!applyXmlEdits(toVector("<a><b></a>"), std::vector<OivXml> {}));
}

void TestContentEdit::addToNothingIsError()
{
    const auto result = applyXmlEdits(toVector(dlclist), std::vector<OivXml> { xml(OivXml::Kind::Add, "/Nope", "<x/>") });
    QVERIFY(!result);
    QCOMPARE(result.error().message, std::string("XPath /Nope selects nothing"));
}

void TestContentEdit::entitiesAreNotExpanded()
{
    std::string bomb = "<?xml version=\"1.0\"?><!DOCTYPE r [<!ENTITY a \"aaaaaaaaaa\">";
    for (char name = 'b'; name <= 'k'; ++name)
        bomb += std::string("<!ENTITY ") + name + " \"" + std::string(10, '&') + "\">";
    bomb += "]><r>&k;</r>";
    const auto result = applyXmlEdits(toVector(bomb), std::vector<OivXml> { xml(OivXml::Kind::Add, "/r", "<x/>") });
    if (result)
        QVERIFY(result->size() < 4096);
}

QTEST_APPLESS_MAIN(TestContentEdit)
#include "tst_content_edit.moc"
