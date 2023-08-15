#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

std::vector<std::string> ignore_list{
    "System Volume Information",
    "$RECYCLE.BIN",
    ".Trash-1000",
    "LG Smart TV",
    "FOUND.000",
    "FOUND.001",
    "FOUND.002",
    "FOUND.003",
};

class Main {
public:
  fs::path dir1;
  fs::path dir2;

  Main(fs::path dir1, fs::path dir2)
      : dir1(std::move(dir1)), dir2(std::move(dir2)) {}

  void compare(fs::directory_iterator dir_it1, fs::directory_iterator dir_it2) {
    std::set<std::string> entries1;
    for (const auto& entry : dir_it1) {
      if (std::find(ignore_list.begin(), ignore_list.end(),
                    entry.path().lexically_relative(dir1)) != ignore_list.end()) {
        std::cout << "Ignoring " << entry << std::endl;
        continue;
      }
      entries1.emplace(entry.path().lexically_relative(dir1).string());
    }
    std::set<std::string> entries2;
    for (const auto& entry : dir_it2) {
      if (std::find(ignore_list.begin(), ignore_list.end(),
                    entry.path().lexically_relative(dir2)) != ignore_list.end()) {
        std::cout << "Ignoring " << entry << std::endl;
        continue;
      }
      entries2.emplace(entry.path().lexically_relative(dir2).string());
    }
    const auto end1 = std::end(entries1);
    const auto end2 = std::end(entries2);
    auto it1 = entries1.begin();
    auto it2 = entries2.begin();

    while (it1 != end1 || it2 != end2) {
      if (it1 != end1 && it2 == end2) {
        std::cout << "Only on " << dir1 << ":\n  " << *it1 << std::endl;
        break;
      }
      if (it1 == end1 && it2 != end2) {
        std::cout << "Only on " << dir2 << ":\n  " << *it2 << std::endl;
        break;
      }
      if (*it1 != *it2) {
        std::cout << "Difference:\n  " << dir1.string() << "\n    " << *it1 << "\n  "
                  << dir2.string() << "\n    " << *it2 << std::endl;
        break;
      }
      // std::cout << "identical: " << *it1 << " vs. " << *it2 << std::endl;
      fs::path p1{dir1 / *it1};
      fs::path p2{dir2 / *it2};
      // std::cout << "paths: " << p1 << " vs. " << p2 << std::endl;
      if (fs::is_directory(p1) && fs::is_directory(p2)) {
        // std::cout << "entering: " << p1 << " vs. " << p2 << std::endl;
        compare(fs::directory_iterator{p1}, fs::directory_iterator{p2});
      } else if (fs::is_directory(p1) || fs::is_directory(p2)) {
        std::cout << "One path is a directory the other is not:\n  "
                  << dir1.string() << "\n    " << *it1 << "\n  "
                  << dir2.string() << "\n    " << *it2 << std::endl;
      } else {
        files.push_back(*it1);
      }
      ++it1;
      ++it2;
    }
  };

  void compareContents() const {
    size_t i = 0;
    std::chrono::steady_clock::time_point begin =
        std::chrono::steady_clock::now();
    int64_t bytesProcessed = 0;
    for (const auto &file : files) {
      ++i;
      std::cout << i << " / " << files.size() << " ("
                << (i * 100 / files.size()) << " %)";
      if (const auto bytes = compareFiles(dir1 / file, dir2 / file)) {
          bytesProcessed += *bytes;
          auto secondsPassed = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::steady_clock::now() - begin)
                                   .count();
          if (secondsPassed != 0) {
            std::cout << " " << (bytesProcessed / 1024 / 1024 / 1024)
                      << " GB processed ("
                      << (double(bytesProcessed) / secondsPassed / 1024.0 /
                          1024.0)
                      << " MB/s)";
          }
          std::cout << std::endl;
      } else {
          std::cout << "\nFile content differs:\n  " << dir1.string() << "\n  "
                    << dir2.string() << "\n    " << file << std::endl;
          break;
      }
    }
  }

private:
  std::optional<size_t> compareFiles(const fs::path &p1,
                                     const fs::path &p2) const {
    std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
    std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

    if (f1.fail() || f2.fail()) {
      return std::nullopt; // file problem
    }

    size_t file_size = f1.tellg();
    if (file_size != f2.tellg()) {
      return std::nullopt; // size mismatch
    }

    // seek back to beginning and use std::equal to compare contents
    f1.seekg(0, std::ifstream::beg);
    f2.seekg(0, std::ifstream::beg);
    if (std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                   std::istreambuf_iterator<char>(),
                   std::istreambuf_iterator<char>(f2.rdbuf()))) {
      return file_size;
    }
    return std::nullopt;
  }

  std::vector<std::string> files;
};


int main(int argc, char *argv[]) {
  using namespace std;

  if (argc < 3) {
    cerr << "error: dir_a/ dir_b/" << endl;
    return 1;
  }

  fs::path dir1 = argv[1];
  fs::path dir2 = argv[2];
  Main m(dir1, dir2);
  fs::directory_iterator it1{m.dir1};
  fs::directory_iterator it2{m.dir2};

  m.compare(it1, it2);
  m.compareContents();
}
