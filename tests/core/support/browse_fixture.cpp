#include "support/browse_fixture.h"

#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

namespace archon::test {

void writeBrowseFixture(const std::filesystem::path &root)
{
    RpfBuilder inner;
    inner.addBinary("deep/c.txt", toVector("deep content"), false);

    RpfBuilder common;
    common.addBinary("data/a.txt", toVector("hello"), false);
    common.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    common.addResource("x.ytd", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    common.addBinary("inner.rpf", inner.build(), false);

    RpfBuilder update;
    update.addBinary("common/data/dlclist.xml", toVector("<SMandatoryPacksData/>"), false);

    writeFile(root / "GTA5.exe", "not a real executable");
    writeFile(root / "common.rpf", common.build());
    writeFile(root / "update" / "update.rpf", update.build());
}

}
