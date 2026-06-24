#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t fnv1a(const std::string& s) {
	uint64_t h = kFnvOffset;
	for (unsigned char c : s) {
		h ^= c;
		h *= kFnvPrime;
	}
	return h;
}

std::string toHex(uint64_t v) {
	static const char digits[] = "0123456789abcdef";
	std::string s(16, '0');
	for (int i = 15; i >= 0; --i) {
		s[i] = digits[v & 0xF];
		v >>= 4;
	}
	return s;
}

// Directory under which per-directory hash caches are stored, honouring
// XDG_CONFIG_HOME / HOME (and APPDATA on Windows), falling back to a temp dir.
fs::path configDir() {
	if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
		return fs::path(xdg) / "dircompare";
	}
	if (const char* home = std::getenv("HOME"); home && *home) {
		return fs::path(home) / ".config" / "dircompare";
	}
#ifdef _WIN32
	if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
		return fs::path(appdata) / "dircompare";
	}
#endif
	std::error_code ec;
	return fs::temp_directory_path(ec) / "dircompare";
}

// Caches file content hashes for a single root directory, keyed by relative
// path. An entry is reused only when the file's modification time and size are
// unchanged, so a second run skips re-reading unmodified files.
class HashCache {
public:
	explicit HashCache(const fs::path& dir) {
		std::error_code ec;
		root = fs::weakly_canonical(dir, ec);
		if (ec) {
			root = fs::absolute(dir, ec);
		}
		cacheFile = configDir() / (toHex(fnv1a(root.string())) + ".cache");
		load();
	}

	~HashCache() {
		if (dirty) {
			save();
		}
	}

	// Returns the content hash of `rel` (relative to the root) and writes its
	// size to `sizeOut`, or nullopt if the file can't be read.
	std::optional<uint64_t> hashOf(const std::string& rel, uint64_t& sizeOut) {
		const fs::path full = root / rel;
		std::error_code ec;
		const uint64_t size = fs::file_size(full, ec);
		if (ec) {
			return std::nullopt;
		}
		const auto writeTime = fs::last_write_time(full, ec);
		if (ec) {
			return std::nullopt;
		}
		const int64_t mtime = writeTime.time_since_epoch().count();
		sizeOut = size;

		if (auto it = entries.find(rel);
		    it != entries.end() && it->second.mtime == mtime && it->second.size == size) {
			return it->second.hash; // cache hit
		}

		const auto hash = computeHash(full);
		if (!hash) {
			return std::nullopt;
		}
		entries[rel] = { mtime, size, *hash };
		dirty = true;
		return *hash;
	}

private:
	struct Entry {
		int64_t mtime;
		uint64_t size;
		uint64_t hash;
	};

	static std::optional<uint64_t> computeHash(const fs::path& p) {
		std::ifstream f(p, std::ifstream::binary);
		if (f.fail()) {
			return std::nullopt;
		}
		uint64_t h = kFnvOffset;
		char buf[1 << 16];
		while (f) {
			f.read(buf, sizeof(buf));
			const std::streamsize n = f.gcount();
			for (std::streamsize i = 0; i < n; ++i) {
				h ^= static_cast<unsigned char>(buf[i]);
				h *= kFnvPrime;
			}
		}
		if (f.bad()) {
			return std::nullopt;
		}
		return h;
	}

	void load() {
		std::ifstream in(cacheFile);
		if (!in) {
			return;
		}
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') {
				continue;
			}
			std::istringstream iss(line);
			int64_t mtime;
			uint64_t size;
			uint64_t hash;
			if (!(iss >> mtime >> size >> hash)) {
				continue;
			}
			std::string rel;
			std::getline(iss, rel);
			if (!rel.empty() && rel[0] == ' ') {
				rel.erase(0, 1);
			}
			entries[rel] = { mtime, size, hash };
		}
	}

	void save() const {
		std::error_code ec;
		fs::create_directories(cacheFile.parent_path(), ec);
		std::ofstream out(cacheFile, std::ios::trunc);
		if (!out) {
			return;
		}
		out << "# dircompare cache v1 " << root.string() << "\n";
		for (const auto& [rel, e] : entries) {
			out << e.mtime << ' ' << e.size << ' ' << e.hash << ' ' << rel << "\n";
		}
	}

	fs::path root;
	fs::path cacheFile;
	std::map<std::string, Entry> entries;
	bool dirty = false;
};

} // namespace

std::vector<std::string> ignore_list{
    ".Spotlight-V100",
    ".TemporaryItems",
    ".Trash-1000",
    ".Trashes",
    "$RECYCLE.BIN",
    "FOUND.000",
    "FOUND.001",
    "FOUND.002",
    "FOUND.003",
    "LG Smart TV",
    "System Volume Information",
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
	  HashCache cache1{ dir1 };
	  HashCache cache2{ dir2 };
	  size_t i = 0;
	  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	  int64_t bytesProcessed = 0;
	  for (const auto& file : files) {
		  ++i;
		  std::cout << i << " / " << files.size() << " (" << (i * 100 / files.size()) << " %)";
		  uint64_t size1 = 0;
		  uint64_t size2 = 0;
		  const auto hash1 = cache1.hashOf(file, size1);
		  const auto hash2 = cache2.hashOf(file, size2);
		  if (hash1 && hash2 && size1 == size2 && *hash1 == *hash2) {
			  bytesProcessed += size1;
			  auto secondsPassed = std::chrono::duration_cast<std::chrono::seconds>(
				                       std::chrono::steady_clock::now() - begin)
				                       .count();
			  if (secondsPassed != 0) {
				  std::cout << " " << (bytesProcessed / 1024 / 1024 / 1024) << " GB processed ("
					        << (double(bytesProcessed) / secondsPassed / 1024.0 / 1024.0)
					        << " MB/s)";
			  }
			  std::cout << std::endl;
		  } else {
			  std::cout << "\nFile content differs:\n  " << dir1.string() << "\n  " << dir2.string()
				        << "\n    " << file << std::endl;
			  break;
		  }
	  }
  }

  private:
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
  std::error_code err;
  fs::directory_iterator it1{m.dir1, err};
  if (err) {
      cerr << "error: " << argv[1] << ": " << err << endl;
      return EXIT_FAILURE;
  }
  fs::directory_iterator it2{m.dir2, err};
  if (err) {
      cerr << "error: " << argv[2] << ": " << err << endl;
      return EXIT_FAILURE;
  }

  m.compare(it1, it2);
  m.compareContents();
}
