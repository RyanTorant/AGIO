#include <assert.h>
#include <algorithm>
#include <enumerate.h>
#include <matplotlibcpp.h>
#include <queue>
#include <random>

#include "../../Core/Config.h"
#include "../../Evolution/Population.h"
#include "../../Evolution/Globals.h"
#include "../../Serialization/SRegistry.h"
#include "../../Utils/Math/Float2.h"

#include "PreyPredator.h"
#include "PublicInterfaceImpl.h"

#include "../Greedy/Greedy.h"

// World data;
WorldData world;

namespace plt = matplotlibcpp;
using namespace agio;
using namespace fpp;
using namespace std;

class Metrics {
public:
    vector<float> fitness_vec_hervibore;
    vector<float> novelty_vec_hervibore;
    vector<float> fitness_vec_carnivore;
    vector<float> novelty_vec_carnivore;

    vector<float> fitness_vec_registry;
    vector<float> novelty_vec_registry;
    vector<float> avg_fitness_herbivore;
    vector<float> avg_progress_herbivore;
    vector<float> avg_fitness_carnivore;
    vector<float> avg_progress_carnivore;

    vector<float> avg_eaten_herbivore;
    vector<float> avg_eaten_carnivore;
    vector<float> avg_failed_herbivore;
    vector<float> avg_failed_carnivore;
    vector<float> avg_coverage_herbivore;
    vector<float> avg_coverage_carnivore;

	vector<float> avg_eaten_herbivore_greedy;
	vector<float> avg_eaten_carnivore_greedy;
	vector<float> avg_failed_herbivore_greedy;
	vector<float> avg_failed_carnivore_greedy;
	vector<float> avg_coverage_herbivore_greedy;
	vector<float> avg_coverage_carnivore_greedy;

    vector<float> avg_fitness_carnivore_random;
    vector<float> avg_fitness_herbivore_random;

    vector<float> avg_fitness_difference;
    vector<float> avg_fitness_network;
    vector<float> avg_fitness_random;
    vector<float> avg_novelty_registry;
    vector<float> species_count;
    vector<float> min_fitness_difference;
    vector<float> max_fitness_difference;

    Metrics()
    {
        plt::ion();
    };

    void update(Population &pop)
    {
        for (const auto&[_, species] : pop.GetSpecies())
        {
            // Find average evaluation metrics
            float avg_eaten = 0;
            float avg_failed = 0;
            float avg_coverage = 0;
            for (int id : species.IndividualsIDs)
            {
                const auto& org = pop.GetIndividuals()[id];
                auto state_ptr = ((OrgState*)org.GetState());

                avg_eaten += (float)state_ptr->EatenCount / state_ptr->Repetitions;
                avg_failed += (float)state_ptr->FailedActionFractionAcc / state_ptr->Repetitions;
                avg_coverage += (float)state_ptr->VisitedCellsCount / state_ptr->Repetitions;
            }

            avg_eaten /= species.IndividualsIDs.size();
            avg_failed /= species.IndividualsIDs.size();
            avg_coverage /= species.IndividualsIDs.size();

            if (((OrgState*)pop.GetIndividuals()[species.IndividualsIDs[0]].GetState())->IsCarnivore)
            {
                avg_eaten_carnivore.push_back(avg_eaten);
                avg_failed_carnivore.push_back(avg_failed);
                avg_coverage_carnivore.push_back(avg_coverage);
            }
            else
            {
                avg_eaten_herbivore.push_back(avg_eaten);
                avg_failed_herbivore.push_back(avg_failed);
                avg_coverage_herbivore.push_back(avg_coverage);
            }
            cout << "    " << avg_eaten << " " << avg_failed << " " << avg_coverage << endl;
        }

		// Force a reset. REFACTOR!
		for (auto& org : pop.GetIndividuals())
		{
			auto state_ptr = (OrgState*)org.GetState();

			state_ptr->MetricsCurrentGenNumber = -1;
			state_ptr->VisitedCellsCount = 0;
			state_ptr->VisitedCells = {};
			state_ptr->EatenCount = 0;
			state_ptr->FailedActionCountCurrent = 0;
			state_ptr->Repetitions = 0;
			state_ptr->FailableActionCount = 0;
			state_ptr->FailedActionFractionAcc = 0;
		}

		pop.SimulateWithUserFunction(&world,createGreedyActionsMap(),
			[&](const MorphologyTag& tag)
			{
				float avg_eaten = 0;
				float avg_failed = 0;
				float avg_coverage = 0;
				const auto& species = pop.GetSpecies().find(tag)->second;

				for (int id : species.IndividualsIDs)
				{
					const auto& org = pop.GetIndividuals()[id];
					auto state_ptr = ((OrgState*)org.GetState());

					avg_eaten += (float)state_ptr->EatenCount / state_ptr->Repetitions;
					avg_failed += (float)state_ptr->FailedActionFractionAcc / state_ptr->Repetitions;
					avg_coverage += (float)state_ptr->VisitedCellsCount / state_ptr->Repetitions;
				}

				avg_eaten /= species.IndividualsIDs.size();
				avg_failed /= species.IndividualsIDs.size();
				avg_coverage /= species.IndividualsIDs.size();

				// Find org type
				bool is_carnivore;
				for (auto[gid, cid] : tag)
				{
					if (gid == 0) // mouth group
					{
						if (cid == 0) // herbivore
							is_carnivore = false;
						else if (cid == 1)
							is_carnivore = true;
					}
				}

				// Update values
				if (is_carnivore)
				{
					avg_eaten_carnivore_greedy.push_back(avg_eaten);
					avg_failed_carnivore_greedy.push_back(avg_failed);
					avg_coverage_carnivore_greedy.push_back(avg_coverage);
				}
				else
				{
					avg_eaten_herbivore_greedy.push_back(avg_eaten);
					avg_failed_herbivore_greedy.push_back(avg_failed);
					avg_coverage_herbivore_greedy.push_back(avg_coverage);
				}

				// Force a reset. REFACTOR!
				for (auto& org : pop.GetIndividuals())
				{
					auto state_ptr = (OrgState*)org.GetState();

					state_ptr->MetricsCurrentGenNumber = -1;
					state_ptr->VisitedCellsCount = 0;
					state_ptr->VisitedCells = {};
					state_ptr->EatenCount = 0;
					state_ptr->FailedActionCountCurrent = 0;
					state_ptr->Repetitions = 0;
					state_ptr->FailableActionCount = 0;
					state_ptr->FailedActionFractionAcc = 0;
				}
			});
    }

    void plot(Population &pop)
    {
        calculate_metrics(pop);
		//return;
        plt::clf();

        /*plt::subplot(2, 3, 1);
        plt::plot(avg_fitness_herbivore, "b");
        plt::plot(avg_fitness_herbivore_random, "r");

        plt::subplot(2, 3, 2);
        plt::plot(avg_fitness_carnivore, "k");
        plt::plot(avg_fitness_carnivore_random, "r");

        plt::subplot(2, 3, 3);
        plt::plot(avg_progress_herbivore, "b");
        plt::plot(avg_progress_carnivore, "k");*/

        //plt::subplot(2, 3, 4);
		plt::subplot(1, 3, 1);
        plt::plot(avg_eaten_herbivore, "b");
        plt::plot(avg_eaten_carnivore, "k");
		plt::plot(avg_eaten_herbivore_greedy, "b--");
		plt::plot(avg_eaten_carnivore_greedy, "k--");

        //plt::subplot(2, 3, 5);
        plt::subplot(1, 3, 2);
        plt::plot(avg_failed_herbivore, "b");
        plt::plot(avg_failed_carnivore, "k");
		plt::plot(avg_failed_herbivore_greedy, "b--");
        plt::plot(avg_failed_carnivore_greedy, "k--");

        //plt::subplot(2, 3, 6);
		plt::subplot(1, 3, 3);
        plt::plot(avg_coverage_herbivore, "b");
        plt::plot(avg_coverage_carnivore, "k");
        plt::plot(avg_coverage_herbivore_greedy, "b--");
        plt::plot(avg_coverage_carnivore_greedy, "k--");

        plt::pause(0.01);
    }

private:
    void calculate_metrics(Population &pop)
    {
        fitness_vec_hervibore.resize(0);
        novelty_vec_hervibore.resize(0);

        for (auto[idx, org] : enumerate(pop.GetIndividuals()))
        {
            if (((OrgState*)org.GetState())->IsCarnivore)
                fitness_vec_carnivore.push_back(org.Fitness);
            else
                fitness_vec_hervibore.push_back(org.Fitness);
        }

        // Only average the best 5
        sort(fitness_vec_hervibore.begin(), fitness_vec_hervibore.end(), [](float a, float b) { return a > b; });
        sort(fitness_vec_carnivore.begin(), fitness_vec_carnivore.end(), [](float a, float b) { return a > b; });

        float avg_f_hervibore = accumulate(fitness_vec_hervibore.begin(),
                                           fitness_vec_hervibore.begin() + min<int>(fitness_vec_hervibore.size(), 5), 0.0f) / 5.0f;

        float avg_f_carnivore = accumulate(fitness_vec_carnivore.begin(),
                                           fitness_vec_carnivore.begin() + min<int>(fitness_vec_carnivore.size(), 5), 0.0f) / 5.0f;

        float progress_carnivore, progress_herbivore, rand_f_carnivore, rand_f_hervibore;

        for (const auto &[_, s] : pop.GetSpecies())
        {
            auto org_state = (OrgState*)pop.GetIndividuals()[s.IndividualsIDs[0]].GetState();
            if (org_state->IsCarnivore)
            {
                progress_carnivore = s.ProgressMetric;
                avg_f_carnivore = s.DevMetrics.RealFitness;
                rand_f_carnivore = s.DevMetrics.RandomFitness;
            } else
            {
                progress_herbivore = s.ProgressMetric;
                avg_f_hervibore = s.DevMetrics.RealFitness;
                rand_f_hervibore = s.DevMetrics.RandomFitness;
            }
        }

        avg_fitness_herbivore_random.push_back(rand_f_hervibore);
        avg_fitness_herbivore.push_back((avg_f_hervibore));
        avg_progress_herbivore.push_back(progress_herbivore);

        avg_fitness_carnivore.push_back((avg_f_carnivore));
        avg_fitness_carnivore_random.push_back(rand_f_carnivore);
        avg_progress_carnivore.push_back(progress_carnivore);
    }

};

agio::Population runEvolution()
{
    minstd_rand RNG(chrono::high_resolution_clock::now().time_since_epoch().count());

    NEAT::load_neat_params("../src/Tests/PreyPredator/NEATConfig.txt");
    NEAT::mutate_morph_param_prob = Settings::ParameterMutationProb;
    NEAT::destructive_mutate_morph_param_prob = Settings::ParameterDestructiveMutationProb;
    NEAT::mutate_morph_param_spread = Settings::ParameterMutationSpread;

    // Create base interface
    Interface = new PublicInterfaceImpl();
    Interface->Init();

    // Create and fill the world
    world.fill(FoodCellCount, WorldSizeX, WorldSizeY);

    // Spawn population
    Population pop;
    pop.Spawn(PopSizeMultiplier, SimulationSize);

    // Do evolution loop
    Metrics metrics;
    for (int g = 0; g < GenerationsCount; g++)
    {
		((PublicInterfaceImpl*)Interface)->CurrentGenNumber = g;

        pop.Epoch(&world, [&](int gen)
        {
			cout << "Generation : " << gen << endl;
            metrics.update(pop);
            cout << endl;

            metrics.plot(pop);
        }, true);

    }
    pop.EvaluatePopulation(&world);
    pop.CurrentSpeciesToRegistry();

    SRegistry registry(&pop);
    registry.save(SerializationFile);

	{
		cout << "Press any letter + enter to exit" << endl;
		int tmp;
		cin >> tmp;
	}

	return pop;
}