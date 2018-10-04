#include "SPopulation.h"
#include "../Evolution/Population.h"

SPopulation::SPopulation() {}

SPopulation::SPopulation(Population *population)
{
    // Save the individuals
    for(Individual &individual : population->Individuals)
        individuals.emplace_back(SIndividual(individual));

    // Save the species map
    int speciesId = 0;
    for (auto const &[_, species]: population->SpeciesMap)
    {
        vector<SIndividual *> speciesIndividuals;
        for (int individualId : species.IndividualsIDs)
        {
            SIndividual *individual = &individuals[individualId];
            individual->species_id = speciesId;

            speciesIndividuals.emplace_back(individual);
        }
        species_map.emplace(speciesId, speciesIndividuals);
        speciesId++;
    }
}
