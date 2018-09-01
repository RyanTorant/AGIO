#include "Population.h"
#include "enumerate.h"
#include "Globals.h"
#include "../Core/Config.h"
#include <numeric>
#include <assert.h>
#include <unordered_set>
#include <queue>

// NEAT
#include "innovation.h"
#include "genome.h"
#include "../NEAT/include/population.h"

using namespace std;
using namespace agio;
using namespace fpp;

Population::Population() : RNG(std::chrono::high_resolution_clock::now().time_since_epoch().count())
{
	// TODO : Create a bigger population and only simulate a few on each step
	cur_node_id = 0;
	cur_innov_num = 0;
	CurrentGeneration = 0;
}

void Population::BuildSpeciesMap()
{
    // clear the species map
	for(auto & [_, s] : SpeciesMap)
    {
        for (auto innovation : s->innovations)
            delete innovation;
		delete s;
    }
    SpeciesMap.clear();

	// put every individual into his corresponding species
	for (auto [idx, org] : enumerate(Individuals))
	{
		auto& tag = org.GetMorphologyTag(); // make a copy
		//tag.Parameters = {}; // remove parameters


		Species* species_ptr = nullptr;
		auto iter = SpeciesMap.find(tag);
		if (iter == SpeciesMap.end())
		{
			species_ptr = new Species;
			SpeciesMap[tag] = species_ptr;
		}
		else
			species_ptr = iter->second;

		species_ptr->IndividualsIDs.push_back(idx);
		org.SpeciesPtr = species_ptr;
	}
}

void Population::Spawn(int SizeMult,int SimSize)
{
	int pop_size = SizeMult * SimSize;

	SimulationSize = SimSize;
	Individuals.resize(pop_size);
	
	for (auto [idx, org] : enumerate(Individuals))
		org.Spawn(idx);

	DominationBuffer.resize(pop_size);
	NoveltyNearestKBuffer.resize(Settings::NoveltyNearestK);
	CompetitionNearestKBuffer.resize(Settings::LocalCompetitionK);
	ChildrenBuffer.resize(pop_size);
	CurrentGeneration = 0;

	// Build species
	for (auto[idx, org] : enumerate(Individuals))
	{
		auto& tag = org.GetMorphologyTag();

		Species* species_ptr = nullptr;
		auto iter = SpeciesMap.find(tag);
		if (iter == SpeciesMap.end())
		{
			species_ptr = new Species;
			SpeciesMap[tag] = species_ptr;
		}
		else
			species_ptr = iter->second;

		species_ptr->IndividualsIDs.push_back(idx);
		org.SpeciesPtr = species_ptr;
	}

	for (auto &[tag, s] : SpeciesMap)
	{
		auto start_genome = new NEAT::Genome(tag.NumberOfSensors, tag.NumberOfActions, 0, 0);
		s->NetworksPopulation = new NEAT::Population(start_genome, s->IndividualsIDs.size());

		// Replace the genomes
		for (auto[idx, org_idx] : enumerate(s->IndividualsIDs))
		{
			// Using % because when neat does "delta encoding" it drops the pop size to half
			// Not needed here, I just copied and pasted
			auto target_genome = s->NetworksPopulation->organisms[idx % s->NetworksPopulation->organisms.size()]->gnome;

			Individuals[org_idx].Genome = target_genome->duplicate(target_genome->genome_id);
			Individuals[org_idx].Brain = Individuals[org_idx].Genome->genesis(target_genome->genome_id);
			Individuals[org_idx].Morphology.Genes.reset(target_genome->duplicate(target_genome->genome_id));
		}
	}

	// TODO :  Separate organisms by distance in the world too
//	BuildSpeciesMap();
}

void Population::Epoch(void * WorldPtr, std::function<void(int)> EpochCallback)
{
	// First version : The species are fixed and created at the start. Also no parameters

	// Evaluate the entire population
	EvaluatePopulation(WorldPtr);

	// Call the callback
	ComputeNovelty(); // <- Just to see how it's changing
	EpochCallback(CurrentGeneration);

	// Advance generation
	CurrentGeneration++;

	// Now do the epoch
	for (auto & [tag, s] : SpeciesMap)
	{
		// First update the fitness values for neat
		auto& pop = s->NetworksPopulation;
		for (auto& neat_org : pop->organisms)
		{
			// Find the organism in the individuals that has this brain
			for (int org_idx : s->IndividualsIDs)
			{
				const auto& org = Individuals[org_idx];
				if (org.Genome->genome_id == neat_org->gnome->genome_id) 
				{
					neat_org->fitness = org.Fitness; // Here you could add some contribution of the novelty
					
					// Remapping because NEAT appears to only work with positive fitness
					neat_org->fitness = log2(exp2(0.1*neat_org->fitness) + 1.0) + 1.0;
					
					break;
				}
			}
		}
	
		// With the fitness updated call the neat epoch
		pop->epoch(CurrentGeneration);

		// Now replace the individuals with the new brains genomes
		for (auto [idx, org_idx] : enumerate(s->IndividualsIDs))
		{
			delete Individuals[org_idx].Genome;
			delete Individuals[org_idx].Brain;

			// Using % because when neat does "delta encoding" it drops the pop size to half
			auto target_genome = pop->organisms[idx % pop->organisms.size()]->gnome;

			Individuals[org_idx].Genome = target_genome->duplicate(target_genome->genome_id);
			Individuals[org_idx].Brain = Individuals[org_idx].Genome->genesis(target_genome->genome_id);
			Individuals[org_idx].Morphology.Genes.reset(target_genome->duplicate(target_genome->genome_id));
		}
	}
}

void Population::EvaluatePopulation(void * WorldPtr)
{
	for (auto& org : Individuals)
		org.AccumulatedFitness = 0;

	// Simulate in batches of SimulationSize
	for (int j = 0; j < Individuals.size() / SimulationSize; j++)
	{
		// TODO : Optimize this, you could pass the current batch id and make the individuals aware of their index in the vector
		for (auto[idx, org] : enumerate(Individuals))
		{
			if (idx >= j * SimulationSize && idx < (j + 1)*SimulationSize)
				org.InSimulation = true;
			else
				org.InSimulation = false;
		}

		for (int i = 0; i < Settings::SimulationReplications; i++)
		{
			// Compute fitness of each individual
			Interface->ComputeFitness(this, WorldPtr);

			// Update the fitness and novelty accumulators
			for (auto& org : Individuals)
				org.AccumulatedFitness += org.Fitness;
		}
	}

	for (auto& org : Individuals)
		org.Fitness = org.AccumulatedFitness / (float)Settings::SimulationReplications;
};
#if 0
void Population::Epoch(void * WorldPtr, std::function<void(int)> EpochCallback)
{
	auto compute_local_scores = [&]()
	{
		for (auto &[tag, species] : SpeciesMap)
		{
			for (int idx : species->IndividualsIDs)
			{
				// Find the K nearest inside the species

				// Clear the K buffer
				for (auto & v : CompetitionNearestKBuffer)
				{
					v.first = -1;
					v.second = numeric_limits<float>::max();
				}
				int k_buffer_top = 0;

				// Check against the species individuals
				for (int other_idx : species->IndividualsIDs)
				{
					if (idx == other_idx)
						continue;

					// This is the morphology distance
					float dist = Individuals[idx].GetMorphologyTag().Distance(Individuals[other_idx].GetMorphologyTag());

					// Check if it's in the k nearest
					for (auto[kidx, v] : enumerate(CompetitionNearestKBuffer))
					{
						if (dist < v.second)
						{
							// Move all the values to the right
							for (int i = Settings::LocalCompetitionK - 1; i > kidx; i--)
								CompetitionNearestKBuffer[i] = CompetitionNearestKBuffer[i - 1];

							if (kidx > k_buffer_top)
								k_buffer_top = kidx;
							v.first = other_idx;
							v.second = dist;
							break;
						}
					}
				}

				// With the k nearest found, check how many of them this individual bests and also compute genotypic diversity
				auto & org = Individuals[idx];
				org.LocalScore = 0;
				org.GenotypicDiversity = 0;
				for(int i = 0;i <= k_buffer_top;i++)
				{
					const auto& other_org = Individuals[CompetitionNearestKBuffer[i].first];
					if (org.Fitness > other_org.Fitness)
						org.LocalScore++;

					org.GenotypicDiversity += org.GetMorphologyTag().Distance(other_org.GetMorphologyTag());
				}
				org.GenotypicDiversity /= k_buffer_top + 1;
			}
		}
	};


	auto dominates = [](const Individual& A, const Individual& B)
	{
		//return A.LocalScore > B.LocalScore;
		return (A.LastNoveltyMetric >= B.LastNoveltyMetric && A.LocalScore >= B.LocalScore  && A.GenotypicDiversity >= B.GenotypicDiversity) &&
			(A.LastNoveltyMetric > B.LastNoveltyMetric || A.LocalScore > B.LocalScore || A.GenotypicDiversity > B.GenotypicDiversity);
	};
	auto compute_domination_fronts = [&]()
	{
		unordered_map<Individual::MorphologyTag, vector<unordered_set<int>>> fronts;

		for (auto &[tag, species] : SpeciesMap)
		{
			vector<unordered_set<int>> fronts_vec;

			unordered_set<int> current_front = {};
			for (int this_idx : species->IndividualsIDs)
			{
				auto& org = Individuals[this_idx];

				org.DominatedSet = {};
				org.DominationCounter = 0;
				for (int other_idx : species->IndividualsIDs)
				{
					if (other_idx == this_idx)
						continue;

					auto& other_org = Individuals[other_idx];
					if (dominates(org, other_org))
						org.DominatedSet.insert(other_idx);
					else if (dominates(other_org, org))
						org.DominationCounter++;
				}

				if (org.DominationCounter == 0)
				{
					org.DominationRank = 0;
					current_front.insert(this_idx);
				}
			}

			int i = 0;
			while (current_front.size() > 0)
			{
				fronts_vec.push_back(current_front);
				unordered_set<int> next_front = {};

				for (int this_idx : current_front)
				{
					auto& org = Individuals[this_idx];
					for (int other_idx : org.DominatedSet)
					{
						auto& other_org = Individuals[other_idx];
						other_org.DominationCounter--;
						if (other_org.DominationCounter == 0)
						{
							other_org.DominationRank = i + 1;
							next_front.insert(other_idx);
						}
					}
				}

				i++;
				current_front = move(next_front);
			}

			fronts[tag] = move(fronts_vec);
		}

		return fronts;
	};
	// Tournament selection (k = 2)
	auto tournament_select = [&](const vector<int>& Parents)
	{
		int p0 = Parents[uniform_int_distribution<int>(0, Parents.size() - 1)(RNG)];
		int p1 = Parents[uniform_int_distribution<int>(0, Parents.size() - 1)(RNG)];
		while (p0 == p1)
			p1 = Parents[uniform_int_distribution<int>(0, Parents.size() - 1)(RNG)];

		// Keep the one with the lower rank (less dominated)
		int r0 = Individuals[p0].DominationRank;
		int r1 = Individuals[p1].DominationRank;
		if (r0 < r1)
			return p0;
		else
		{
			if (r1 < r0)
				return p1;
			else
				return (uniform_int_distribution<int>(0, 1)(RNG) ? p0 : p1);
		}
	};

	// Reset innovations
	for (auto &[tag, species] : SpeciesMap)
	{
		for (auto innovation : species->innovations)
			delete innovation;
		species->innovations.resize(0);
	}

	if (CurrentGeneration == 0)
	{
		ComputeNovelty();
		EvaluatePopulation(WorldPtr);
		compute_local_scores();

		// Select parents based on the non dominated fronts
		// Not using the crowding distance because novelty already cares about diversity
		Children.resize(0);

		// TODO : For now, generating the same number of children as individuals on the species
		//   this should be changed, the species size should change based on fitness
		for (auto & [tag, species] : SpeciesMap)
		{
			if (species->IndividualsIDs.size() == 1)
			{
				// Only one individual on the species, so just clone it
				// TODO : Find if there's some better way to handle this
				const auto& individual = Individuals[species->IndividualsIDs[0]];
				Children.emplace_back(individual, Individual::Make::Clone);
				continue;
			}

			// On the first generation generate everyone can be a parent

			for (int n = 0; n < species->IndividualsIDs.size(); n++)
			{
				int mom_idx = tournament_select(species->IndividualsIDs);
				int dad_idx = tournament_select(species->IndividualsIDs);
				while (mom_idx == dad_idx)
					dad_idx = tournament_select(species->IndividualsIDs); // TODO : I think this gets stuck if you have only 2 individuals where one is dominated by the other

				Children.emplace_back(Individuals[mom_idx], Individuals[dad_idx], Children.size());

				// Mutate
				if (uniform_real_distribution<float>()(RNG) <= Settings::ChildMutationProb)
					Children.back().Mutate(this, CurrentGeneration);
			}
		}
	}
	else
	{
		vector<Individual> old_pop = move(Individuals);
		size_t children_size = Children.size();
		Individuals = move(Children);

		// Generate species map for the children
		BuildSpeciesMap();

		// Evaluate
		ComputeNovelty();
		EvaluatePopulation(WorldPtr);
		compute_local_scores();

		for (auto &[tag, species] : SpeciesMap)
		{
			if (species->IndividualsIDs.size() == 1)
			{
				// Only one individual on the species, so just clone it
				// TODO : Find if there's some better way to handle this
				const auto& individual = Individuals[species->IndividualsIDs[0]];
				Children.emplace_back(individual, Individual::Make::Clone);
				continue;
			}
		}

		// Make a copy of the species map
		decltype(SpeciesMap) next_pop_species_map;
		for (auto &[tag, species] : SpeciesMap)
		{
			auto new_ptr = new Species;
			new_ptr->IndividualsIDs = species->IndividualsIDs;
			next_pop_species_map[tag] = new_ptr;

			for (int idx : species->IndividualsIDs)
				Individuals[idx].SpeciesPtr = new_ptr;
		}

		// Merge populations and generate a species map for both
		for (auto& org : old_pop)
			Individuals.push_back(move(org));
		BuildSpeciesMap();

		// Recompute novelty and local competition scores
		ComputeNovelty();
		compute_local_scores();

		// Compute fronts
		auto fronts_map = compute_domination_fronts();

		// TODO : For now, generating the same number of children as individuals on the species
		//   this should be changed, the species size should change based on fitness
		for (auto &[tag, species] : SpeciesMap) // the population at this point is a mixture of the children and parents
		{
			// Search for this species in the map of what the next population would be
			auto next_iter = next_pop_species_map.find(tag);

			if (next_iter == next_pop_species_map.end() || 
				species->IndividualsIDs.size() == 1 ||
				next_iter->second->IndividualsIDs.size() == 1)
				continue;

			auto& front = fronts_map[tag];

			vector<int> parents;
			int number_of_children = next_iter->second->IndividualsIDs.size();
			parents.reserve(number_of_children);

			int i = 0;
			while (parents.size() < number_of_children)
			{
				for (int idx : front[i])
				{
					if (parents.size() == number_of_children)
						break;

					parents.push_back(idx);
				}

				i++;
			}

			for (int n = 0; n < number_of_children; n++)
			{
				int mom_idx = tournament_select(parents);
				int dad_idx = tournament_select(parents);
				while (mom_idx == dad_idx)
					dad_idx = tournament_select(parents); // TODO : I think this gets stuck if you have only 2 individuals where one is dominated by the other

				Children.emplace_back(Individuals[mom_idx], Individuals[dad_idx], Children.size());
				
				// Mutate
				if (uniform_real_distribution<float>()(RNG) <= Settings::ChildMutationProb)
					Children.back().Mutate(this, CurrentGeneration);
			}
		}

		// Restore species
		for (auto &[_, s] : SpeciesMap)
		{
			for (auto innovation : s->innovations)
				delete innovation;
			delete s;
		}
		SpeciesMap.clear();
		SpeciesMap = next_pop_species_map;

		// Remove the old pop from the individuals
		// Children are first
		vector<Individual> temp_vec;
		for (int i = 0;i < children_size;i++)
			temp_vec.push_back(move(Individuals[i]));
		Individuals = move(temp_vec);

		ComputeNovelty();
	}

	EpochCallback(CurrentGeneration);
	CurrentGeneration++;

	return;
}
#endif
void Population::ComputeNovelty()
{
	for (auto[idx, org] : enumerate(Individuals))
	{
		// Clear the K buffer
		for (auto & v : NoveltyNearestKBuffer)
			v = numeric_limits<float>::max();
		int k_buffer_top = 0;

		// Check both against the population and the registry
		for (auto[otherIdx, other] : enumerate(Individuals))
		{
			if (idx == otherIdx)
				continue;

			// This is the morphology distance
			float dist = org.GetMorphologyTag().Distance(other.GetMorphologyTag());

			// Check if it's in the k nearest
			for (auto[kidx, v] : enumerate(NoveltyNearestKBuffer))
			{
				if (dist < v)
				{
					// Move all the values to the right
					for (int i = Settings::NoveltyNearestK - 1; i > kidx; i--)
						NoveltyNearestKBuffer[i] = NoveltyNearestKBuffer[i - 1];

					if (kidx > k_buffer_top)
						k_buffer_top = kidx;
					v = dist;
					break;
				}
			}
		}

		for (auto & tag : MorphologyRegistry)
		{
			// This is the morphology distance
			float dist = org.GetMorphologyTag().Distance(tag);

			// Check if it's in the k nearest
			for (auto[kidx, v] : enumerate(NoveltyNearestKBuffer))
			{
				if (dist < v)
				{
					// Move all the values to the right
					for (int i = Settings::NoveltyNearestK - 1; i > kidx; i--)
						NoveltyNearestKBuffer[i] = NoveltyNearestKBuffer[i - 1];

					if (kidx > k_buffer_top)
						k_buffer_top = kidx;
					v = dist;
					break;
				}
			}
		}

		// After the k nearest are found, compute novelty metric
		// It's simply the average of the k nearest distances
		float novelty = accumulate(NoveltyNearestKBuffer.begin(), NoveltyNearestKBuffer.begin() + k_buffer_top + 1, 0) / float(NoveltyNearestKBuffer.size());
		org.LastNoveltyMetric = novelty;

		// If the novelty is above the threshold, add it to the registry if it doesn't exist already
		if (novelty > Settings::NoveltyThreshold)
		{
			const auto& tag = org.GetMorphologyTag();
			
			// Using a vector instead of a set because iterations are much more common than insertions
			if (find(MorphologyRegistry.begin(), MorphologyRegistry.end(), tag) == MorphologyRegistry.end())
				MorphologyRegistry.push_back(tag);
		}
	}
}


Population::ProgressMetrics Population::ComputeProgressMetrics(void * World)
{
	// TODO : For some reason this function is affecting the results of neat!
	return {};

	ProgressMetrics metrics;

	// Compute average novelty
	ComputeNovelty();
	metrics.AverageNovelty = 0;
	for (const auto& org : Individuals)
		metrics.AverageNovelty += org.LastNoveltyMetric;
	metrics.AverageNovelty /= Individuals.size();

	// Do a few simulations to compute fitness
	EvaluatePopulation(World);

	// Backup the evaluated fitness value
	for (auto &org : Individuals)
		org.BackupFitness = org.Fitness;

	for (auto &org : Individuals)
		metrics.AverageFitness += org.Fitness;
	metrics.AverageFitness /= Individuals.size();

	// Now for each species set that to be random, and do a couple of simulations
	metrics.AverageFitnessDifference = 0;
	metrics.MaxFitnessDifference = -numeric_limits<float>::max();
	metrics.MinFitnessDifference = numeric_limits<float>::max();
	for (const auto&[_, species] : SpeciesMap)
	{
		// First compute average fitness of the species when using the network
		// Only consider the 5 best

		float avg_f = 0;
		priority_queue<float> fitness_queue;

		// TODO : Move this to the config file
		const int QueueSize = 5;

		for (int org_id : species->IndividualsIDs)
		{
			fitness_queue.push(-Individuals[org_id].Fitness); // using - because the priority queue is from high to low
			while (fitness_queue.size() > QueueSize)
				fitness_queue.pop();
		}

		float queue_size = fitness_queue.size();
		assert(queue_size == QueueSize);
		while (!fitness_queue.empty())
		{
			avg_f += -fitness_queue.top();
			fitness_queue.pop();
		}
		avg_f /= queue_size;

			//avg_f += Individuals[org_id].Fitness;
		//avg_f /= species->IndividualsIDs.size();

		// Override the use of the network for decision-making
		for (int org_id : species->IndividualsIDs)
			Individuals[org_id].UseNetwork = false;

		// Do a few simulations
		float avg_random_f = 0;
		float count = 0;
		for (int org_id : species->IndividualsIDs)
			Individuals[org_id].AccumulatedFitness = 0;
		
		for (int i = 0; i < Settings::SimulationReplications; i++)
		{
			Interface->ComputeFitness(this, World);

			for (int org_id : species->IndividualsIDs)
				Individuals[org_id].AccumulatedFitness += Individuals[org_id].Fitness / (float)Settings::SimulationReplications;
		}

		for (int org_id : species->IndividualsIDs)
		{
			fitness_queue.push(-Individuals[org_id].AccumulatedFitness); // using - because the priority queue is from high to low
			while (fitness_queue.size() > QueueSize)
				fitness_queue.pop();
		}

		queue_size = fitness_queue.size();
		assert(queue_size == QueueSize);
		while (!fitness_queue.empty())
		{
			avg_random_f += -fitness_queue.top();
			fitness_queue.pop();
		}
		avg_random_f /= queue_size;


		// Accumulate difference
		const float epsilon = 1e-6;
		metrics.AverageFitnessDifference += avg_f - avg_random_f;
		metrics.MaxFitnessDifference = max(metrics.MaxFitnessDifference, avg_f - avg_random_f);
		metrics.MinFitnessDifference = min(metrics.MinFitnessDifference, avg_f - avg_random_f);

		//metrics.AverageFitnessDifference += 100.0f * (avg_f - avg_random_f) / (fabsf(avg_random_f) + 1.0f);
		//metrics.MaxFitnessDifference = max(metrics.MaxFitnessDifference, 100.0f * (avg_f - avg_random_f) / (fabsf(avg_random_f) + 1.0f));
		//metrics.MinFitnessDifference = min(metrics.MinFitnessDifference, 100.0f * (avg_f - avg_random_f) / (fabsf(avg_random_f) + 1.0f));

		// Re-enable the network
		for (int org_id : species->IndividualsIDs)
			Individuals[org_id].UseNetwork = true;
	}
	metrics.AverageFitnessDifference /= SpeciesMap.size();

	// Restore the old fitness values
	for (auto &org : Individuals)
		org.Fitness = org.BackupFitness;

	return metrics;
}


#if 0
Population::ProgressMetrics Population::ComputeProgressMetrics(void * World)
{
	ProgressMetrics metrics;

	// Compute average novelty
	ComputeNovelty();
	metrics.AverageNovelty = 0;
	for (const auto& org : Individuals)
		metrics.AverageNovelty += org.LastNoveltyMetric;
	metrics.AverageNovelty /= Individuals.size();

	// Do a few simulations to compute fitness
    metrics.AverageFitnessDifference = 0;
    metrics.AverageFitness = 0;
    metrics.AverageRandomFitness = 0;

	EvaluatePopulation(World);

    for (auto &org : Individuals)
        metrics.AverageFitness += org.Fitness;
    metrics.AverageFitness /= Individuals.size();

    // Now for each species set that to be random, and do a couple of simulations
    // Override the use of the network for decision-making
    for (const auto &[_, species] : SpeciesMap)
        for (int org_id : species->IndividualsIDs)
            Individuals[org_id].UseNetwork = false;

	EvaluatePopulation(World);

    for (auto &org : Individuals)
        metrics.AverageRandomFitness += org.Fitness;
    metrics.AverageRandomFitness /= Individuals.size();

    // Re-enable the network
    for (const auto &[_, species] : SpeciesMap)
        for (int org_id : species->IndividualsIDs)
            Individuals[org_id].UseNetwork = true;

    metrics.AverageFitnessDifference =
            ((metrics.AverageFitness - metrics.AverageRandomFitness) / metrics.AverageRandomFitness) * 100;

	// TODO : Compute standard deviations

	return metrics;
}
#endif