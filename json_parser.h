#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

struct Stock {
    std::string symbol;
    std::string name;
    double base_price;
    double tick_size;
};

class StockConfigParser {
private:
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r\"");
        size_t last = str.find_last_not_of(" \t\n\r\",");
        return (first == std::string::npos) ? "" : str.substr(first, last - first + 1);
    }
    
    std::string extractValue(const std::string& line, const std::string& key) {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        
        pos = line.find(":", pos);
        if (pos == std::string::npos) return "";
        
        pos++;
        size_t start = line.find_first_not_of(" \t", pos);
        size_t end = line.find_first_of(",}", start);
        
        if (start == std::string::npos || end == std::string::npos) 
            return trim(line.substr(start));
        
        return trim(line.substr(start, end - start));
    }

public:
    std::vector<Stock> loadStocks(const std::string& filename) {
        std::vector<Stock> stocks;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            std::cerr << "JSON dosyası açılamadı: " << filename << std::endl;
            return stocks;
        }
        
        std::string line;
        Stock currentStock;
        bool inStock = false;
        
        while (std::getline(file, line)) {
            if (line.find("{") != std::string::npos && line.find("\"stocks\"") == std::string::npos) {
                inStock = true;
                currentStock = Stock();
            }
            
            if (inStock) {
                if (line.find("\"symbol\"") != std::string::npos) {
                    currentStock.symbol = extractValue(line, "symbol");
                }
                else if (line.find("\"name\"") != std::string::npos) {
                    currentStock.name = extractValue(line, "name");
                }
                else if (line.find("\"base_price\"") != std::string::npos) {
                    std::string priceStr = extractValue(line, "base_price");
                    currentStock.base_price = std::stod(priceStr);
                }
                else if (line.find("\"tick_size\"") != std::string::npos) {
                    std::string tickStr = extractValue(line, "tick_size");
                    currentStock.tick_size = std::stod(tickStr);
                }
                
                    if (line.find("}") != std::string::npos && line.find("{") == std::string::npos) {
                    if (!currentStock.symbol.empty()) {
                        stocks.push_back(currentStock);
                    }
                    inStock = false;
                }
            }
        }
        
        file.close();
        return stocks;
    }
};

#endif