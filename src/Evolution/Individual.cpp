#include "Individual.h"
#include "Globals.h"
#include <random>
#include "enumerate.h"
#include "zip.h"
#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <assert.h>
#include "../Core/Config.h"
#include <bitset>
#include "Population.h"

// NEAT
#include "neat.h"
#include "network.h"
#include "population.h"
#include "organism.h"
#include "genome.h"
#include "species.h"

using namespace agio;
using namespace std;
using namespace fpp;

Individual::Individual() : RNG(chrono::high_resolution_clock::now().time_since_epoch().count())
{
    State = nullptr;
    Genome = nullptr;
    Brain = nullptr;
    LastFitness = -1;
    LastNoveltyMetric = -1;
    GlobalID = CurrentGlobalID.fetch_add(1);
	SpeciesPtr = nullptr;
	LastDominationCount = -1;
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
            Parameter p;
            p.ID = pidx;
            p.Value = 0.5f * (param.Min + param.Max);
            p.HistoricalMarker = 0;

            // Small shift
            p.Value +=
                    normal_distribution<float>(0, Settings::ParameterMutationSpread)(RNG) * abs(param.Max - param.Min);

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
	Morphology.GenesIDs.resize(Genome->genes.size());
	for (auto[gene_id, gene] : zip(Morphology.GenesIDs, Genome->genes))
		gene_id = gene->innovation_num;
}

void Individual::DecideAndExecute(void *World, const class Population *PopulationPtr)
{
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
    LastFitness = -1;
    LastNoveltyMetric = -1;
	LastDominationCount = -1;
}


Individual::Individual(const Individual& Mom, const Individual& Dad, int ChildID) : Individual()
{
    assert(Mom.Morphology == Dad.Morphology);

    // TODO : Use the other mating functions, and test which is better
    // We don't do inter-species mating
    Genome = Mom.Genome->mate_multipoint_avg(Dad.Genome, ChildID, Mom.LastFitness, Dad.LastFitness, false);
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
		Components = Components;
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
	Morphology.GenesIDs.resize(Genome->genes.size());
	for (auto [gene_id, gene] : zip(Morphology.GenesIDs, Genome->genes))
		gene_id = gene->innovation_num;

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
    // The distance is computed as the number of different action and sensors and the total parameter dist
    float dist = 0;

    for (auto[bf0, bf1] : zip(ActionsBitfield, Other.ActionsBitfield))
        dist += std::bitset<sizeof(bf0)>(bf0 ^ bf1).count();

    for (auto[bf0, bf1] : zip(SensorsBitfield, Other.SensorsBitfield))
        dist += std::bitset<sizeof(bf0)>(bf0 ^ bf1).count();

    // TODO : Find a better way to combine param and actions/sensors distance
    for (const auto &[idx, p0] : Parameters)
    {
        auto param_iter = Other.Parameters.find(idx);

        // Check that the parameter exists
        if (param_iter != Other.Parameters.end())
        {
            // Normalize when computing distance by parameter range
            /*const auto& param_def = Interface->GetParameterDefRegistry()[p0.ID];
            dist += fabsf(p0.Value - param_iter->second.Value) / (param_def.Max - param_def.Min);*/
            // Another option : Add one if the historical markers are different
            if (p0.HistoricalMarker != param_iter->second.HistoricalMarker)
                dist += 1;
        } else
            // Mismatching parameter, take it into account
            // TODO : Find a better value than just adding one?
            dist += 1;
    }

    // Traverse the parameters of the other individual to see if there are some that don't exist on this
    for (const auto &[idx, p1] : Other.Parameters)
    {
        auto param_iter = Parameters.find(idx);
        if (param_iter == Parameters.end())
            dist += 1; // TODO : Idem as the last case
    }

    // Finally check number of mismatching genes on the genome of the control network
    dist += abs((int)GenesIDs.size() - (int) Other.GenesIDs.size());

    for (const auto &gene : GenesIDs)
    {
        bool found = false;

		for (const auto& other_gene : GenesIDs)
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


    return dist;
}

void Individual::Mutate(Population *pop, int generation)
{
    auto randfloat = [this]()
    {
        return uniform_real_distribution<float>()(RNG);
    };

    // TODO : Implement mutation of components!

    if (randfloat() < Settings::NEAT::mutate_add_node_prob)
    {
        Genome->mutate_add_node(SpeciesPtr->innovations, pop->cur_node_id, pop->cur_innov_num);
    }
    else if (randfloat() < Settings::NEAT::mutate_add_link_prob)
    {
        // TODO: Find out why genesis is done in neat code (species.cpp 585)
        NEAT::Network *net_analogue = Genome->genesis(generation);
        Genome->mutate_add_link(SpeciesPtr->innovations, pop->cur_innov_num, NEAT::newlink_tries);
        delete net_analogue;
    }
    // NOTE:  A link CANNOT be added directly after a node was added because the phenotype
    //        will not be appropriately altered to reflect the change
    else
    {
        //If we didn't do a structural mutation, we do the other kinds
        if (randfloat() < Settings::NEAT::mutate_random_trait_prob)
            Genome->mutate_random_trait();

        if (randfloat() < Settings::NEAT::mutate_link_trait_prob)
            Genome->mutate_link_trait(1);

        if (randfloat() < Settings::NEAT::mutate_node_trait_prob)
            Genome->mutate_node_trait(1);

        if (randfloat() < Settings::NEAT::mutate_link_weights_prob)
            Genome->mutate_link_weights(Settings::NEAT::weight_mut_power, 1.0, NEAT::mutator::GAUSSIAN);

        if (randfloat() < Settings::NEAT::mutate_toggle_enable_prob)
            Genome->mutate_toggle_enable(1);

        if (randfloat() < Settings::NEAT::mutate_gene_reenable_prob)
            Genome->mutate_gene_reenable();
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
            } else
            {
                normal_distribution<float> distribution(param.Value, Settings::ParameterMutationSpread);
                param.Value = distribution(RNG);
            }
        }
    }
}

Individual::Individual(Individual && other)
{
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
	RNG = move(other.RNG);
	Morphology = move(other.Morphology);

	other.Genome = nullptr;
	other.Brain = nullptr;
	other.State = nullptr;
}

Individual::~Individual()
{
	if (Brain) delete Brain;
	if (Genome) delete Genome;
	if (State) Interface->DestroyState(State);
}