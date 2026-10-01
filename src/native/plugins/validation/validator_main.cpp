#include "plugins/validation/adapter_validator.h"

#include <filesystem>
#include <iostream>
#include <system_error>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: vr_adapter_validator <manifest.json>\n";
    return 2;
  }

  std::error_code error;
  if (!std::filesystem::is_regular_file(argv[1], error)) {
    std::cerr << "adapter manifest is missing or unreadable\n";
    return 1;
  }

  std::cout << "adapter manifest is readable: " << argv[1] << '\n';
  std::cout << "load-compatible: manifest-only\n";
  std::cout << "target-compatible: manifest-only\n";
  std::cout << "runtime-safe: manifest-only\n";
  return 0;
}
