#include "utils.hpp"
#include <fstream>
#include <sstream>

namespace utils {

bool write_file(const std::string &path, const std::string &contents)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.good()) {
        return false;
    }

    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return f.good();
}

bool read_file(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace utils
