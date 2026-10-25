// SPDX-License-Identifier: GPL-3.0-or-later
#include <ReadBarcode.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace {
std::string quoted(const std::string &value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += char(c);
    }
    return result + '"';
}
}
int main(int argc, char **argv) {
    try {
        if (argc != 3 || std::string(argv[1]) != "-json")
            throw std::runtime_error("Usage: uxz-code-reader -json input.ppm");
        std::ifstream input(argv[2], std::ios::binary);
        std::string magic;
        int width = 0, height = 0, maximum = 0;
        input >> magic >> width >> height >> maximum;
        if (!input || magic != "P6" || width <= 0 || height <= 0 || maximum != 255 ||
            width > 32768 || height > 32768 || int64_t(width) * height > 100000000)
            throw std::runtime_error("Unsupported or oversized PPM input");
        if (input.get() == '\r' && input.peek() == '\n') input.get();
        std::vector<unsigned char> bytes(size_t(width) * height * 3);
        input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
        if (input.gcount() != std::streamsize(bytes.size()))
            throw std::runtime_error("Incomplete PPM input");
        const auto results = ZXing::ReadBarcodes(ZXing::ImageView(bytes.data(), width, height, ZXing::ImageFormat::RGB));
        std::cout << '[';
        bool first = true;
        for (const auto &code : results) {
            if (!code.isValid()) continue;
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"format\":" << quoted(ZXing::ToString(code.format()))
                      << ",\"text\":" << quoted(code.text()) << '}';
        }
        std::cout << "]\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
