#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include "../../monitor/features.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <window_samples.csv>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << argv[1] << std::endl;
        return 1;
    }

    float ax[128], ay[128], az[128];
    float temp_c = 25.0f;
    float temp_prev = 25.0f;
    float current_a = 0.0f;

    std::string line;

    std::getline(file, line);

    int count = 0;
    while (std::getline(file, line) && count < 128) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, ',')) {
            parts.push_back(token);
        }
        if (parts.size() >= 10) {

            ax[count] = std::stof(parts[4]);
            ay[count] = std::stof(parts[5]);
            az[count] = std::stof(parts[6]);
            if (count == 0) {
                temp_c = std::stof(parts[7]);
                temp_prev = temp_c;
                try {
                    current_a = std::stof(parts[8]);
                } catch (...) {
                    current_a = 0.0f;
                }
            }
            count++;
        }
    }

    if (count < 128) {
        std::cerr << "Expected 128 samples, got " << count << std::endl;
        return 1;
    }

    float features[16];
    extract_features(ax, ay, az, temp_c, temp_prev, current_a, features);

    std::cout << std::fixed << std::setprecision(6);
    for (int i = 0; i < 16; i++) {
        std::cout << features[i] << (i < 15 ? "," : "\n");
    }

    return 0;
}
