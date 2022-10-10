#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::string> ignore_list{
    "System Volume Information",
    "$RECYCLE.BIN",
    ".Trash-1000",
    "LG Smart TV",
};

class Main {
public:
  fs::path dir1;
  fs::path dir2;

  Main(fs::path dir1, fs::path dir2)
      : dir1(std::move(dir1)), dir2(std::move(dir2)) {}

  void compare(fs::directory_iterator dir_it1, fs::directory_iterator dir_it2) const {
    std::set<std::string> entries1;
    for (const auto& entry : dir_it1) {
      if (std::find(ignore_list.begin(), ignore_list.end(),
                    entry.path().lexically_relative(dir1)) != ignore_list.end()) {
        std::cout << "Ignoring " << entry << std::endl;
        continue;
      }
      entries1.emplace(entry.path().lexically_relative(dir1));
    }
    std::set<std::string> entries2;
    for (const auto& entry : dir_it2) {
      if (std::find(ignore_list.begin(), ignore_list.end(),
                    entry.path().lexically_relative(dir2)) != ignore_list.end()) {
        std::cout << "Ignoring " << entry << std::endl;
        continue;
      }
      entries2.emplace(entry.path().lexically_relative(dir2));
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
      }
      ++it1;
      ++it2;
    }
  };
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
}
