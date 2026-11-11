#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <cstdio>

int main(int argc, char *argv[])
{
    using namespace archon::test;
    if (argc != 2) {
        std::fputs("usage: write_fuzz_seed <directory>\n", stderr);
        return 1;
    }
    RpfBuilder inner;
    inner.addBinary("data/a.txt", toVector("hello"), false);
    inner.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    inner.addResource("x.ytd", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    RpfBuilder outer;
    outer.addBinary("sub/inner.rpf", inner.build(), false);
    outer.addBinary("readme.txt", toVector("seed"), true);
    const std::filesystem::path directory(argv[1]);
    writeFile(directory / "plain.rpf", inner.build());
    writeFile(directory / "open.rpf", inner.build(archon::RpfEncryption::Open));
    writeFile(directory / "nested.rpf", outer.build());
    return 0;
}
