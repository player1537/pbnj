#ifndef PBNJ_CONFIGURATION_H
#define PBNJ_CONFIGURATION_H

#include <ConfigReader.h>

#include <string>

namespace pbnj {

    class Configuration {

        public:
            Configuration(std::string filename);

            std::string configFilename;

            std::string dataFilename;
            int dataXDim;
            int dataYDim;
            int dataZDim;

            int imageWidth;
            int imageHeight;
            std::string imageFilename;

        private:
            ConfigReader *reader;
    };

}

#endif
