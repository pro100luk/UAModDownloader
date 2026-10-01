#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cfg {
    class Config {
        public:
            Config();

            bool getStrictSearch();
            void setStringSearch(bool strict);
            bool getWireframe() { return wireframe; }
            void setWireframe(bool wireframeEnabled);

            void saveConfig();
        private:
            void loadConfig();
            void parseConfig();

            nlohmann::json config;
            bool is_strict;
            bool wireframe;
    };
}