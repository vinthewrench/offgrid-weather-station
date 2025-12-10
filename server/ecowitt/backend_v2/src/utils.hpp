#pragma once
#include <string>

namespace utils {

bool write_file(const std::string &path, const std::string &contents);
bool read_file(const std::string &path, std::string &out);

}
