#include <unordered_set>
#include <chrono>
#include <bitset>
#include <list>
#include <cassert>

#include "enumerate.h"
#include "zip.h"
#include "Individual.h"
#include "Population.h"
#include "../Core/Config.h"

// NEAT
#include "neat.h"
#include "network.h"
#include "organism.h"

using namespace agio;
using namespace std;
using namespace fpp;

Individual::Individual() : RNG(chrono::high_resolution_clock::now().time_since_epoch().count())
{
    State = nullptr;
    Genome = nullptr;
    Brain = nullptr;
    Fitness = 0;
    LastNoveltyMetric = 0;
    OriginalID = GlobalID = CurrentGlobalID.fetch_add(1);
    SpeciesPtr = nullptr;
    LastDominationCount = -1;


    AccumulatedFitness = 0;
    //AccumulatedNovelty = 0;
}

void Individual::Spawn(int ID)
{
    assert(Interface->GetActionRegistry().size() > 0);
    assert(Interface->GetSensorRegistry().size() > 0);

    // TODO : Find if there's a way to avoid the memory allocations
    unordered_set<int> actions_set;
    unordered_set<int> sensors_set;

    // Construct the components list
    for (auto[gidx, group] : enumerate(Interface->GetComponentRegistry()))
    {
        // TODO : Profile what's the best way to do this
        int components_count = uniform_int_distribution<int>(group.MinCardinality, group.MaxCardinality)(RNG);

        vector<int> index_vec(group.Components.size());
        for (auto[idx, v] : enumerate(index_vec)) v = idx;

        shuffle(index_vec.begin(), index_vec.end(), RNG);

        for (int i = 0; i < components_count; i++)
        {
            Components.push_back({(int) gidx, index_vec[i]});

            const auto &component = group.Components[index_vec[i]];

            actions_set.insert(component.Actions.begin(), component.Actions.end());
            sensors_set.insert(component.Sensors.begin(), component.Sensors.end());
        }
    }

    // Construct the parameters list
    // Initially, spawn the parameters in the middle, with the same historical marker
    // after spawning, shift them a bit randomly.
    // This allows to cross parameters that are similar, by keeping track of the historical markers
    // A mutation can change the parameter, and it'll get a new historical marker
    for (auto[pidx, param] : enumerate(Interface->GetParameterDefRegistry()))
    {
        // If the parameter is not required, select it randomly, 50/50 chance
        // TODO : Maybe make the selection probability a parameter? Does it makes sense?
        if (param.IsRequired || uniform_int_distribution<int>(0, 1)(RNG))
        {
            Parameter p{};
            p.ID = pidx;
            p.Value = 0.5f * (param.Min + param.Max);
            p.HistoricalMarker = Parameter::CurrentMarkerID;

            // Small shift
            float shift = normal_distribution<float>(0, Settings::ParameterMutationSpread)(RNG);
            p.Value +=  shift * abs(param.Max - param.Min);

            // Clamp values
            p.Value = clamp(p.Value, param.Min, param.Max);

            Parameters[param.UserID] = p;
        }
    }

    // Convert the action and sensors set to vectors
    Actions.resize(actions_set.size());
    for (auto[idx, action] : enumerate(actions_set))
        Actions[idx] = action;

    Sensors.resize(sensors_set.size());
    for (auto[idx, sensor] : enumerate(sensors_set))
        Sensors[idx] = sensor;

    // Sort the actions and the sensors vectors
    // This is important! Otherwise mating between individuals is meaningless, because the order is arbitrary
    //  and the same input could mean different things for two individuals of the same species
    sort(Actions.begin(), Actions.end());
    sort(Sensors.begin(), Sensors.end());

    SensorsValues.resize(Sensors.size());
    ActivationsBuffer.resize(Actions.size());

    // Create base genome and network
    Genome = new NEAT::Genome(Sensors.size(), Actions.size(), 0, 0);

    // Randomize weights and traits
    // The settings are taken from NEAT/src/population.cpp, line 280
    Genome->mutate_link_weights(1, 1, NEAT::COLDGAUSSIAN);
    Genome->randomize_traits();

    // Construct network from genome
    Brain = Genome->genesis(Genome->genome_id);

    // Create a new state
    State = Interface->MakeState(this);

    // Fill bitfields
    Morphology.ActionsBitfield.resize((Interface->GetActionRegistry().size() - 1) / 64 + 1);
    fill(Morphology.ActionsBitfield.begin(), Morphology.ActionsBitfield.end(), 0);
    for (auto action : Actions)
        // I'm hoping that the compiler is smart enough to change the / and % to ands and shift
        Morphology.ActionsBitfield[action / 64ull] |= 1ull << (action % 64ull);

    Morphology.SensorsBitfield.resize((Interface->GetSensorRegistry().size() - 1) / 64 + 1);
    fill(Morphology.SensorsBitfield.begin(), Morphology.SensorsBitfield.end(), 0);
    for (auto sensor : Sensors)
        // I'm hoping that the compiler is smart enough to change the / and % to ands and shift
        Morphology.SensorsBitfield[sensor / 64ull] |= 1ull << (sensor % 64ull);

    Morphology.Parameters = Parameters;
    Morphology.NumberOfActions = Actions.size();
    Morphology.NumberOfSensors = Sensors.size();
	Morphology.Genes.reset(Genome->duplicate(Genome->genome_id));
    /*Morphology.GenesIDs.resize(Genome->genes.size());
    for (auto[gene_id, gene] : zip(Morphology.GenesIDs, Genome->genes))
        gene_id = gene->innovation_num;*/
}

void Individual::DecideAndExecute(void *World, const class Population *PopulationPtr)
{
	if (!UseNetwork)
	{
		uniform_int_distribution<int> action_dist(0, Actions.size() - 1);
		Interface->GetActionRegistry()[Actions[action_dist(RNG)]].Execute(State, PopulationPtr, this, World);
		return;
	}

    // Load sensors
    for (auto[value, idx] : zip(SensorsValues, Sensors))
        value = Interface->GetSensorRegistry()[idx].Evaluate(State, World, PopulationPtr, this);

    // Send sensors to brain and activate
    Brain->load_sensors(SensorsValues);
    bool sucess = Brain->activate(); // TODO : Handle failure

    // Select action based on activations
    float act_sum = 0;
    for (auto[idx, v] : enumerate(Brain->outputs))
        act_sum += ActivationsBuffer[idx] = v->activation; // The activation function is in [0, 1], check line 461 of neat.cpp

    int action;
    if (act_sum > 1e-6)
    {
        discrete_distribution<int> action_dist(begin(ActivationsBuffer), end(ActivationsBuffer));
        action = action_dist(RNG);
    } else
    {
        // Can't decide on an action because all activations are 0
        // Select one action at random
        uniform_int_distribution<int> action_dist(0, Actions.size() - 1);
        action = action_dist(RNG);
    }

    // Finally execute the action
    Interface->GetActionRegistry()[Actions[action]].Execute(State, PopulationPtr, this, World);
}

void Individual::Reset()
{
    Interface->ResetState(State);
    if (Brain) Brain->flush();
    Fitness = 0;
    LastNoveltyMetric = 0;
    LastDominationCount = -1;
}

Individual::Individual(const Individual &Parent, Individual::Make) : Individual()
{
    OriginalID = Parent.OriginalID;
    LastDominationCount = Parent.LastDominationCount;
    Fitness = Parent.Fitness;
    LastNoveltyMetric = Parent.LastNoveltyMetric;

    Actions = Parent.Actions;
    Sensors = Parent.Sensors;
    ActivationsBuffer.resize(Parent.Actions.size());
    SensorsValues.resize(Parent.Sensors.size());
    Components = Parent.Components;
    Parameters = Parent.Parameters;
    Morphology = Parent.Morphology;
	
    State = Interface->DuplicateState(Parent.State);

    Genome = Parent.Genome->duplicate(GlobalID);
    Brain = Genome->genesis(Genome->genome_id);

    AccumulatedFitness = Parent.AccumulatedFitness;
    //AccumulatedNovelty = Parent.AccumulatedNovelty;
    //EvaluationsCount = Parent.EvaluationsCount;

	SpeciesPtr = Parent.SpeciesPtr;
};

Individual::Individual(const Individual &Mom, const Individual &Dad, int ChildID) : Individual()
{
	assert(Mom.SpeciesPtr == Dad.SpeciesPtr);
	SpeciesPtr = Mom.SpeciesPtr;

    // TODO : Check if this is correct
    // Because now we can have old individuals mixed with the new ones, let's use the global id as child id
    ChildID = GlobalID;

    // TODO : Use the other mating functions, and test which is better
    // We don't do inter-species mating
    Genome = Mom.Genome->mate_multipoint_avg(Dad.Genome, ChildID, Mom.Fitness, Dad.Fitness, false);
    Brain = Genome->genesis(ChildID);

    // This vectors are the same for both parents
    Actions = Mom.Actions;
    Sensors = Mom.Sensors;
    ActivationsBuffer.resize(Mom.Actions.size());
    SensorsValues.resize(Mom.Sensors.size());

    // TODO : Find a way to mix the components.
    // You can't trivially swap because you need to respect groups cardinalities
    // For now, just take the components of one parent randomly
    // Usually this vectors are equal between parents, so it shouldn't be that much of a difference
    if (uniform_int_distribution<int>(0, 1)(RNG))
        Components = Mom.Components;
    else
        Components = Dad.Components;

    // Cross parameters using the historical markers
    // Taking as base the parameters of the first
    // That decision is arbitrary
    // TODO : Randomly choose between mom or dad?
    Parameters = Mom.Parameters;
    for (auto &[idx, cparam] : Parameters)
    {
        // Search for this parameter in the other individual
        auto param_iter = Dad.Parameters.find(idx);
        if (param_iter != Dad.Parameters.end())
        {
            // Also check that the parameters have the same historical marker
            // This way you don't cross parameters that are far apart
            if (param_iter->second.HistoricalMarker == cparam.HistoricalMarker)
            {
                // Average the value of this parent with the other parent
                cparam.Value = 0.5f * (cparam.Value + param_iter->second.Value);
            }
        }
    }

    // Build morphology
    Morphology.NumberOfActions = Mom.Morphology.NumberOfActions;
    Morphology.NumberOfSensors = Mom.Morphology.NumberOfSensors;
    Morphology.ActionsBitfield = Mom.Morphology.ActionsBitfield;
    Morphology.SensorsBitfield = Mom.Morphology.SensorsBitfield;
    Morphology.Parameters = Parameters;
	Morphology.Genes.reset(Genome->duplicate(Genome->genome_id));
    /*Morphology.GenesIDs.resize(Genome->genes.size());
    for (auto[gene_id, gene] : zip(Morphology.GenesIDs, Genome->genes))
        gene_id = gene->innovation_num;*/

    // Finally construct a new state
    State = Interface->MakeState(this);
}

bool Individual::MorphologyTag::IsCompatible(const Individual::MorphologyTag &Other) const
{
    // Two organisms are compatible if they have the same set of actions and sensors and the parameters match up
    if (NumberOfActions != Other.NumberOfActions)
        return false;

    if (NumberOfSensors != Other.NumberOfSensors)
        return false;

    // Check bitfields
    for (auto[bf0, bf1] : zip(ActionsBitfield, Other.ActionsBitfield))
        if (bf0 != bf1) return false;
    for (auto[bf0, bf1] : zip(SensorsBitfield, Other.SensorsBitfield))
        if (bf0 != bf1) return false;

    // Everything is equal, so they are compatible
    return true;
}

bool Individual::MorphologyTag::operator==(const Individual::MorphologyTag &Other) const
{
    // Check first if the are compatible
    if (!IsCompatible(Other))
        return false;

    // They are compatible, so check that the parameters line up
    if (Parameters.size() != Other.Parameters.size())
        return false;

    for (const auto &[idx, param] : Parameters)
    {
        auto param_iter = Other.Parameters.find(idx);

        // Check that the parameters are the same and that they have the same historical origin
        // TODO : Maybe instead of this one should use a distance threshold
        if (param_iter == Other.Parameters.end() || param.HistoricalMarker != param_iter->second.HistoricalMarker)
            return false;
    }

    return true;
}

float Individual::MorphologyTag::Distance(const Individual::MorphologyTag &Other) const
{
    float dist = 0;

	// Find number of different actions
    for (auto[bf0, bf1] : zip(ActionsBitfield, Other.ActionsBitfield))
        dist += std::bitset<sizeof(bf0)>(bf0 ^ bf1).count();

	// Find number of different sensors
    for (auto[bf0, bf1] : zip(SensorsBitfield, Other.SensorsBitfield))
        dist += std::bitset<sizeof(bf0)>(bf0 ^ bf1).count();

	// Find mismatching parameters
	for (const auto &[idx, p0] : Parameters)
    {
        auto param_iter = Other.Parameters.find(idx);

        // Check that the parameter exists
        if (param_iter != Other.Parameters.end())
        {
			// Add one if the historical markers are different
            if (p0.HistoricalMarker != param_iter->second.HistoricalMarker)
                dist += 1;
        } 
		else
            // Mismatching parameter
            dist += 1;
    }

    // Traverse the parameters of the other individual to see if there are some that don't exist on this
    for (const auto &[idx, p1] : Other.Parameters)
    {
        auto param_iter = Parameters.find(idx);
        if (param_iter == Parameters.end())
            dist += 1;
    }

	// TODO : TO USE THIS YOU MUST LOAD THE NEAT PARAMETERS !!
	dist += logf(Genes->compatibility(Other.Genes.get()) + 1.0f);

    // Finally check number of mismatching genes on the genome of the control network
    /*for (const auto &gene : GenesIDs)
    {
        bool found = false;

        for (const auto &other_gene : Other.GenesIDs)
        {
            if (gene == other_gene)
            {
                found = true;
                break;
            }
        }

        if (!found)
            dist += 1;
    }

	for (const auto &gene : Other.GenesIDs)
	{
		bool found = false;

		for (const auto &other_gene : GenesIDs)
		{
			if (gene == other_gene)
			{
				found = true;
				break;
			}
		}

		if (!found)
			dist += 1;
	}*/


    return dist;
}

void Individual::Mutate(Population *population, int generation)
{
    // If there was any mutation at all, and this individual was a clone of a previous one, it stops beign a copy
    // so the original ID needs to change
    bool any_mutation = false;

    auto randfloat = [this]()
    {
        return uniform_real_distribution<float>()(RNG);
    };

    // Mutate components
    if (randfloat() < Settings::ComponentMutationProb)
    {
        // Choose at random one group to mutate a component
        int group_idx = uniform_int_distribution<int>(0, Interface->GetComponentRegistry().size() - 1)(RNG);
        const ComponentGroup &group = Interface->GetComponentRegistry()[group_idx];

        // The component to be mutated
        int component_idx = uniform_int_distribution<int>(0, group.Components.size() - 1)(RNG);

        int group_count = 0;
        for (auto &c : Components)
            if (c.GroupID == group_idx)
                group_count++;

        if (randfloat() < Settings::ComponentAddProbability && group.MaxCardinality < group_count)
        {
            // If the individual hasn't the chosen component yet, add it
            bool found = false;
            for (auto &c : Components)
            {
                if (c.GroupID == group_idx && c.ComponentID == component_idx)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                Components.push_back({group_idx, component_idx});
                any_mutation = true;
            }
        } else if (randfloat() < Settings::ComponentRemoveProbability && group.MinCardinality < group_count)
        {
            for (auto it = Components.begin(); it != Components.end(); it++)
            {
                if (it->GroupID == group_idx && it->ComponentID == component_idx)
                {
                    Components.erase(it);
                    any_mutation = true;
                    break;
                }
            }
        } else if (randfloat() < Settings::ComponentChangeProbability)
        {
            // If the individual has the chosen component, swap it for one at random of the same group
            for (auto it = Components.begin(); it != Components.end(); it++)
            {
                if (it->GroupID == group_idx && it->ComponentID == component_idx)
                {
                    int replacement_idx = uniform_int_distribution<int>(0, group.Components.size() - 1)(RNG);
                    *it = {group_idx, replacement_idx};
                    any_mutation = true;
                    break;
                }
            }

        }
        if (any_mutation)
        {
            unordered_set<int> actions_set;
            unordered_set<int> sensors_set;

            for (const auto &compRef : Components)
            {
                const ComponentGroup &group = Interface->GetComponentRegistry()[compRef.GroupID];
                const auto &component = group.Components[compRef.ComponentID];
                actions_set.insert(component.Actions.begin(), component.Actions.end());
                sensors_set.insert(component.Sensors.begin(), component.Sensors.end());
            }

            std::vector<int> newActions(actions_set.size());
            std::vector<int> newSensors(sensors_set.size());

            for (auto[idx, action] : enumerate(actions_set))
                newActions[idx] = action;

            for (auto[idx, sensor] : enumerate(sensors_set))
                newSensors[idx] = sensor;

            sort(newActions.begin(), newActions.end());
            sort(newSensors.begin(), newSensors.end());

            if (newActions != Actions || newSensors != Sensors)
            {
                Actions = newActions;
                Sensors = newSensors;

                SensorsValues.resize(Sensors.size());
                ActivationsBuffer.resize(Actions.size());

                // Create base genome and network
                Genome = new NEAT::Genome(Sensors.size(), Actions.size(), 0, 0);

                // Randomize weights and traits
                // The settings are taken from NEAT/src/population.cpp, line 280
                Genome->mutate_link_weights(1, 1, NEAT::COLDGAUSSIAN);
                Genome->randomize_traits();

                // Construct network from genome
                Brain = Genome->genesis(Genome->genome_id);

                // Fill bitfields
                Morphology.ActionsBitfield.resize((Interface->GetActionRegistry().size() - 1) / 64 + 1);
                fill(Morphology.ActionsBitfield.begin(), Morphology.ActionsBitfield.end(), 0);
                for (auto action : Actions)
                    // I'm hoping that the compiler is smart enough to change the / and % to ands and shift
                    Morphology.ActionsBitfield[action / 64ull] |= 1ull << (action % 64ull);

                Morphology.SensorsBitfield.resize((Interface->GetSensorRegistry().size() - 1) / 64 + 1);
                fill(Morphology.SensorsBitfield.begin(), Morphology.SensorsBitfield.end(), 0);
                for (auto sensor : Sensors)
                    // I'm hoping that the compiler is smart enough to change the / and % to ands and shift
                    Morphology.SensorsBitfield[sensor / 64ull] |= 1ull << (sensor % 64ull);

                Morphology.Parameters = Parameters;
                Morphology.NumberOfActions = Actions.size();
                Morphology.NumberOfSensors = Sensors.size();
				Morphology.Genes.reset(Genome->duplicate(Genome->genome_id));
                /*Morphology.GenesIDs.resize(Genome->genes.size());
                for (auto[gene_id, gene] : zip(Morphology.GenesIDs, Genome->genes))
                    gene_id = gene->innovation_num;*/
            }
        }
    } else
    {
        // Only mutate network and parameters if the components were not mutated
        // Mutate NEAT network
        if (randfloat() < Settings::NEAT::MutateAddNodeProb)
        {
            Genome->mutate_add_node(SpeciesPtr->innovations, population->cur_node_id, population->cur_innov_num);
            any_mutation = true;
        } else if (randfloat() < Settings::NEAT::MutateAddLinkProb)
        {
            // TODO: Find out why genesis is done in neat code (species.cpp 585)
            NEAT::Network *net_analogue = Genome->genesis(generation);
            Genome->mutate_add_link(SpeciesPtr->innovations, population->cur_innov_num, NEAT::newlink_tries);
            delete net_analogue;
            any_mutation = true;
        } else
        {
            // NOTE:  A link CANNOT be added directly after a node was added because the phenotype
            //        will not be appropriately altered to reflect the change

            //If we didn't do a structural mutation, we do the other kinds
            if (randfloat() < Settings::NEAT::MutateRandomTraitProb)
            {
                Genome->mutate_random_trait();
                any_mutation = true;
            }

            if (randfloat() < Settings::NEAT::MutateLinkTraitProb)
            {
                Genome->mutate_link_trait(1);
                any_mutation = true;
            }
            if (randfloat() < Settings::NEAT::MutateNodeTraitProb)
            {
                Genome->mutate_node_trait(1);
                any_mutation = true;
            }
            if (randfloat() < Settings::NEAT::MutateLinkWeightsProb)
            {
                Genome->mutate_link_weights(Settings::NEAT::WeightMutPower, 1.0, NEAT::mutator::GAUSSIAN);
                any_mutation = true;
            }
            if (randfloat() < Settings::NEAT::MutateToggleEnableProb)
            {

                Genome->mutate_toggle_enable(1);
                any_mutation = true;
            }

            if (randfloat() < Settings::NEAT::MutateGeneReenableProb)
            {
                Genome->mutate_gene_reenable();
                any_mutation = true;
            }

        }

        // Mutate all parameters with certain probability
        for (auto &[idx, param] : Parameters)
        {
            if (uniform_real_distribution<float>()(RNG) <= Settings::ParameterMutationProb)
            {
                if (uniform_real_distribution<float>()(RNG) <= Settings::ParameterDestructiveMutationProb)
                {
                    auto &parameterDef = Interface->GetParameterDefRegistry()[idx];
                    uniform_real_distribution<float> distribution(parameterDef.Min, parameterDef.Max);
                    param.Value = distribution(RNG);
                    param.HistoricalMarker = Parameter::CurrentMarkerID.fetch_add(1);// Create a new historical marker

                    any_mutation = true;
                } else
                {
                    normal_distribution<float> distribution(param.Value, Settings::ParameterMutationSpread);
                    param.Value = distribution(RNG);

                    any_mutation = true;
                }
            }
        }
    }

    // If it was a clone and it mutated, it's no longer a clone
	if (any_mutation && GlobalID != OriginalID)
	{
		OriginalID = GlobalID;
		// Also reset the accumulators, as they are no longer valid
		AccumulatedFitness = 0;
		//AccumulatedNovelty = 0;
		//EvaluationsCount = 0;
	}
        
}

Individual::Individual(Individual &&other) noexcept
{
    OriginalID = other.OriginalID;
    GlobalID = other.GlobalID;
    State = other.State;
    Components = move(other.Components);
    Parameters = move(other.Parameters);
    Genome = other.Genome;
    Brain = other.Brain;
    Actions = move(other.Actions);
    Sensors = move(other.Sensors);
    SensorsValues = move(other.SensorsValues);
    ActivationsBuffer = move(other.ActivationsBuffer);
    RNG = other.RNG;
    Morphology = move(other.Morphology);

    LastDominationCount = other.LastDominationCount;
    Fitness = other.Fitness;
    LastNoveltyMetric = other.LastNoveltyMetric;


    AccumulatedFitness = other.AccumulatedFitness;
    //AccumulatedNovelty = other.AccumulatedNovelty;
    //EvaluationsCount = other.EvaluationsCount;
	

	DominatedSet = move(other.DominatedSet);
	DominationCounter = other.DominationCounter;
	DominationRank = other.DominationCounter;
	LocalScore = other.LocalScore;
	GenotypicDiversity = other.GenotypicDiversity;

    // Careful with this, you don't exactly know if it's still valid
    SpeciesPtr = other.SpeciesPtr;

    other.Genome = nullptr;
    other.Brain = nullptr;
    other.State = nullptr;
}

Individual::~Individual()
{
    delete Brain;
    delete Genome;
    if (State) Interface->DestroyState(State);
}