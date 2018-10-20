#pragma once

#include <vector>
#include <unordered_map>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>

#include "SIndividual.h"

namespace agio {
    class Population;
};

using namespace std;
using namespace agio;

class SPopulation {
public:
    vector<SIndividual> individuals;
    unordered_map<int, vector<SIndividual*>> species_map;

    SPopulation();
    SPopulation(Population *population);

    static SPopulation load(std::string filename);

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
        ar & individuals;
        ar & species_map;
    }
};