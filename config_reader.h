#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

class ConfigReader {
private:
    std::map<std::string, std::map<std::string, std::string> > data;

public:
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Config dosyası açılamadı: " << filename << std::endl;
            return false;
        }

        std::string line, section;
            if (line.empty() || line[0] == '#' || line[0] == ';') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                section = line.substr(1, line.length() - 2);
                continue;
            }

            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);

                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                data[section][key] = value;
            }
        }

        file.close();
        return true;
    }

    std::string get(const std::string& section, const std::string& key, const std::string& defaultValue = "") {
        if (data.find(section) != data.end() && data[section].find(key) != data[section].end()) {
            return data[section][key];
        }
        return defaultValue;
    }

    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) {
        std::string value = get(section, key);
        if (!value.empty()) {
            try {
                return std::stoi(value);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
};

#endif