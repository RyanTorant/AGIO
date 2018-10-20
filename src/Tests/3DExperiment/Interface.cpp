#include "Interface.h"
#include "../../Evolution/Population.h"
#include <math.h>
#include <chrono>

using namespace std;
using namespace agio;

float2 Circle::GetSamplePoint(minstd_rand0 RNG)
{
	float th = uniform_real<float>(0, 2 * 3.1416f)(RNG);
	return float2(cosf(th), sinf(th)) * uniform_real<float>(0,Radius)(RNG);
}

ExperimentInterface::ExperimentInterface()
	: RNG(chrono::high_resolution_clock::now().time_since_epoch().count())
{
}

void ExperimentInterface::ResetState(void * State)
{
	auto state_ptr = (OrgState*)State;
	
	state_ptr->EatenCount = 0;
	state_ptr->Life = GameplayParams::StartingLife;
	state_ptr->Score = 0;
	state_ptr->HasDied = false;

	// Set a random initial orientation
	{
		float theta = uniform_real_distribution<float>(0, 2 * 3.1416f)(RNG);
		state_ptr->Orientation.x = cosf(theta);
		state_ptr->Orientation.y = sinf(theta);
	}

	// Find a random spawn area and spawn inside it
	int spawn_idx = uniform_int_distribution<>(0, World.SpawnAreas.size())(RNG);
	state_ptr->Position = World.SpawnAreas[spawn_idx].GetSamplePoint(RNG);
}

void * ExperimentInterface::MakeState(const Individual * org)
{
	auto state = new OrgState;
	
	ResetState(state);
	
	return state;
}

void ExperimentInterface::DestroyState(void * State)
{
	auto state_ptr = (OrgState*)State;
	delete state_ptr;
}

void * ExperimentInterface::DuplicateState(void * State)
{
	auto new_state = new OrgState;

	*new_state = *(OrgState*)State;

	return new_state;
}

void ExperimentInterface::ComputeFitness(Population * Pop, void *)
{
	for (auto& org : Pop->GetIndividuals())
		org.Reset();

	// Reset the number of individuals eating plants
	for (auto& area : World.PlantsAreas)
		area.NumEatingInside = 0;

	for (int i = 0; i < ExperimentParams::MaxSimulationSteps; i++)
	{
		for (auto& org : Pop->GetIndividuals())
		{
			if (!org.InSimulation)
				continue;

			auto state_ptr = (OrgState*)org.GetState();
			state_ptr->Life -= GameplayParams::LifeLostPerTurn;
			org.Fitness = state_ptr->Score;

			if (state_ptr->Life <= 0)
			{
				// Reset it
				float old_score = state_ptr->Score;
				ResetState(state_ptr);

				// Remember that the org died and it's score
				state_ptr->Score = old_score;
				state_ptr->HasDied = true;
			}

			org.DecideAndExecute(&World, Pop);

			if (!state_ptr->HasDied)
			{
				state_ptr->Score += state_ptr->Life;
				org.Fitness = state_ptr->Score;
			}
		}
	}
}

void ExperimentInterface::Init()
{
	// Fill actions
	ActionRegistry.resize((int)ActionID::Count);

	ActionRegistry[(int)ActionID::Walk] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			state_ptr->Position += GameplayParams::WalkSpeed*state_ptr->Orientation;
			state_ptr->Position = GameplayParams::GameArea.ClampPos(state_ptr->Position);
		}
	);
	ActionRegistry[(int)ActionID::Run] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			state_ptr->Position += GameplayParams::RunSpeed*state_ptr->Orientation;
			state_ptr->Position = GameplayParams::GameArea.ClampPos(state_ptr->Position);
		}
	);
	ActionRegistry[(int)ActionID::TurnLeft] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			// TODO : This could be precomputed
			float th = -GameplayParams::TurnRadians; // Left = counterclockwise
			state_ptr->Position.x = state_ptr->Position.dot(float2(cosf(th), -sinf(th)));
			state_ptr->Position.y = state_ptr->Position.dot(float2(sinf(th), cosf(th)));
		}
	);
	ActionRegistry[(int)ActionID::TurnRight] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			// TODO : This could be precomputed
			float th = GameplayParams::TurnRadians; // Right = clockwise
			state_ptr->Position.x = state_ptr->Position.dot(float2(cosf(th), -sinf(th)));
			state_ptr->Position.y = state_ptr->Position.dot(float2(sinf(th), cosf(th)));
		}
	);
	ActionRegistry[(int)ActionID::EatCorpse] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's dead and nearby
			const auto& tag = Org->GetMorphologyTag();
			
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				// Check first if it's dead
				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life > 0)
					continue;

				// Check if it's within range
				if ((other_state_ptr->Position - state_ptr->Position).length() < GameplayParams::EatDistance) // TODO : Use squared distance to save a sqrt call
				{
					// Now that we know it's in range, check if it's from a different species
					// Doing this at last because it requires iterating over the component list
					if (tag != individual.GetMorphologyTag())
					{
						any_eaten = true;
						other_state_ptr->EatenCount++;
						state_ptr->Life += GameplayParams::EatCorpseLifeGained;

						if (other_state_ptr->EatenCount > GameplayParams::CorpseBitesDuration)
						{
							// Reset it
							float old_score = other_state_ptr->Score;
							ResetState(other_state_ptr);

							// Remember that the org died and it's score
							other_state_ptr->Score = old_score;
							other_state_ptr->HasDied = true;
						}

						break;
					}
				}
			}

			// There's a configurable (could be 0) penalty for doing an action without a valid target
			if (!any_eaten)
				state_ptr->Score -= GameplayParams::WastedActionPenalty;
		}
	);
	ActionRegistry[(int)ActionID::EatPlant] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void*) 
		{
			auto state_ptr = (OrgState*)State;
			bool any_eaten = false;
			
			// Check if the organism is inside an area with plants
			for (auto& plant_area : World.PlantsAreas)
			{
				if (plant_area.Area.IsInside(state_ptr->Position))
				{
					any_eaten = true;
					plant_area.NumEatingInside++;
					state_ptr->Life += GameplayParams::EatPlantLifeGained / float(plant_area.NumEatingInside);
				}
			}

			// There's a configurable (could be 0) penalty for doing an action without a valid target
			if (!any_eaten)
				state_ptr->Score -= GameplayParams::WastedActionPenalty;
		}
	);
	ActionRegistry[(int)ActionID::Attack] = Action
	(
		[&](void * State, const Population * Pop,Individual * Org, void * World) 
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's not dead and nearby
			const auto& tag = Org->GetMorphologyTag();
			
			bool any_attacked = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				// Check first if the individual is alive
				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				// Check if it's within range
				if ((other_state_ptr->Position - state_ptr->Position).length() < GameplayParams::EatDistance) // TODO : Use squared distance to save a sqrt call
				{
					// Now that we know it's in range, change if it's from a different species
					// Doing this at last because it requires iterating over the component list
					if (tag != individual.GetMorphologyTag())
					{
						any_attacked = true;

						other_state_ptr->Life -= GameplayParams::AttackDamage;

						break;
					}
				}
			}

			// There's a configurable (could be 0) penalty for doing an action without a valid target
			if (!any_attacked)
				state_ptr->Score -= GameplayParams::WastedActionPenalty;
		}
	);

	// Fill sensors
	SensorRegistry.resize((int)SensorID::Count);

	SensorRegistry[(int)SensorID::CurrentLife] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			return ((OrgState*)State)->Life;
		}
	);
	// Normalized position
	SensorRegistry[(int)SensorID::CurrentPosX] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			return GameplayParams::GameArea.Normalize(((OrgState*)State)->Position).x;
		}
	);
	SensorRegistry[(int)SensorID::CurrentPosY] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			return GameplayParams::GameArea.Normalize(((OrgState*)State)->Position).y;
		}
	);
	SensorRegistry[(int)SensorID::CurrentOrientationX] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			return ((OrgState*)State)->Orientation.x;
		}
	);
	SensorRegistry[(int)SensorID::CurrentOrientationY] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			return ((OrgState*)State)->Orientation.y;
		}
	);
	SensorRegistry[(int)SensorID::DistanceNearestPartner] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a the same species
			const auto& tag = Org->GetMorphologyTag();
			const auto& species = (*Pop->GetSpecies().find(tag)).second;

			// TODO : All this "search nearest" queries could be made much faster by using a KD-tree

			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (int id : species.IndividualsIDs)
			{
				const auto& individual = Pop->GetIndividuals()[id];

				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				min_dist = min(min_dist, dist);
			}

			return min_dist;
		}
	);
	SensorRegistry[(int)SensorID::DistanceNearestCompetidor] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's not dead
			const auto& tag = Org->GetMorphologyTag();
			
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				min_dist = min(min_dist, dist);
			}

			return min_dist;
		}
	);
	SensorRegistry[(int)SensorID::DistanceNearestCorpse] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's dead
			const auto& tag = Org->GetMorphologyTag();
			
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				// Check first if it hasn't been eaten too many times or if it's life is > 0
				// You can only eat dead organisms that haven't been eaten too many times
				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life > 0 || other_state_ptr->EatenCount >= GameplayParams::CorpseBitesDuration)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				min_dist = min(min_dist, dist);
			}

			return min_dist;
		}
	);
	SensorRegistry[(int)SensorID::DistanceNearestPlantArea] = Sensor
	(
		[](void * State, void * WorldPtr, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			float min_dist = numeric_limits<float>::max();
			for (const auto& [area,_] : ((WorldData*)WorldPtr)->PlantsAreas)
			{
				float dist = (area.Center - state_ptr->Position).length();
				min_dist = min(min_dist, dist);
			}

			return min_dist;
		}
	);

	SensorRegistry[(int)SensorID::LifeNearestCompetidor] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's not dead
			const auto& tag = Org->GetMorphologyTag();
			
			float life_nearest = 0;
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				if (dist < min_dist)
				{
					min_dist = dist;
					life_nearest = other_state_ptr->Life;
				}
			}

			return life_nearest;
		}
	);

	SensorRegistry[(int)SensorID::AngleNearestPartner] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a the same species
			const auto& tag = Org->GetMorphologyTag();
			const auto& species = (*Pop->GetSpecies().find(tag)).second;

			// TODO : All this "search nearest" queries could be made much faster by using a KD-tree

			float2 nearest_pos;
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (int id : species.IndividualsIDs)
			{
				const auto& individual = Pop->GetIndividuals()[id];

				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				if (dist < min_dist)
				{
					min_dist = dist;
					nearest_pos = other_state_ptr->Position;
				}
			}

			return acosf((nearest_pos - state_ptr->Position).normalize().dot(state_ptr->Orientation));
		}
	);
	SensorRegistry[(int)SensorID::AngleNearestCompetidor] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's not dead
			const auto& tag = Org->GetMorphologyTag();
			
			float2 nearest_pos;
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life <= 0)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				if (dist < min_dist)
				{
					min_dist = dist;
					nearest_pos = other_state_ptr->Position;
				}
			}

			return acosf((nearest_pos - state_ptr->Position).normalize().dot(state_ptr->Orientation));
		}
	);
	SensorRegistry[(int)SensorID::AngleNearestCorpse] = Sensor
	(
		[](void * State, void * World, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			// Find an organism of a different species that's dead
			const auto& tag = Org->GetMorphologyTag();
			
			float2 nearest_pos;
			float min_dist = numeric_limits<float>::max();
			bool any_eaten = false;
			for (auto& individual : Pop->GetIndividuals())
			{
				// Ignore individuals that aren't being simulated right now
				// Also, don't do all the other stuff against yourself. 
				// You already know you don't want to eat yourself
				if (!individual.InSimulation || individual.GetGlobalID() == Org->GetGlobalID())
					continue;

				// Check first if it hasn't been eaten too many times or if it's life is > 0
				// You can only eat dead organisms that haven't been eaten too many times
				auto other_state_ptr = individual.GetState<OrgState>();
				if (other_state_ptr->Life > 0 || other_state_ptr->EatenCount >= GameplayParams::CorpseBitesDuration)
					continue;

				float dist = (other_state_ptr->Position - state_ptr->Position).length();
				if (dist < min_dist)
				{
					min_dist = dist;
					nearest_pos = other_state_ptr->Position;
				}
			}

			return acosf((nearest_pos - state_ptr->Position).normalize().dot(state_ptr->Orientation));
		}
	);
	SensorRegistry[(int)SensorID::AngleNearestPlantArea] = Sensor
	(
		[](void * State, void * WorldPtr, const Population* Pop, const Individual * Org)
		{
			auto state_ptr = (OrgState*)State;

			float2 nearest_pos;
			float min_dist = numeric_limits<float>::max();
			for (const auto& [area,_] : ((WorldData*)WorldPtr)->PlantsAreas)
			{
				float dist = (area.Center - state_ptr->Position).length();
				if (dist < min_dist)
				{
					min_dist = dist;
					nearest_pos = area.Center;
				}
			}

			return acosf((nearest_pos - state_ptr->Position).normalize().dot(state_ptr->Orientation));
		}
	);
	

	// Fill components
	// Similar to the PreyPredator demo, but on 3D
	ComponentRegistry.push_back
	({
		1,1, // "Mouth"
		{
			// Herbivore
			{
				{(int)ActionID::EatPlant},
				{
					(int)SensorID::AngleNearestPlantArea,
					(int)SensorID::DistanceNearestPlantArea
				},
			},
			// Carnivore
			{
				{
					(int)ActionID::Attack,
					(int)ActionID::EatCorpse
				},
				{
					(int)SensorID::AngleNearestCorpse,
					(int)SensorID::DistanceNearestCorpse,
					(int)SensorID::LifeNearestCompetidor
				}
			}
		}
	});

	ComponentRegistry.push_back
	({
		1,1, // This sensors and actions are common for all organisms
		{
			{
				{
					(int)ActionID::Walk,
					(int)ActionID::Run,
					(int)ActionID::TurnLeft,
					(int)ActionID::TurnRight
				},
				{
					(int)SensorID::CurrentLife,
					(int)SensorID::CurrentPosX,
					(int)SensorID::CurrentPosY,
					(int)SensorID::CurrentOrientationX,
					(int)SensorID::CurrentOrientationY,
					(int)SensorID::DistanceNearestPartner,
					(int)SensorID::DistanceNearestCompetidor,
					(int)SensorID::AngleNearestPartner,
					(int)SensorID::AngleNearestCompetidor,
				}
			}
		}
	});
}

float GameplayParams::WalkSpeed;
float GameplayParams::RunSpeed;
Rect GameplayParams::GameArea;
float GameplayParams::TurnRadians;
float GameplayParams::EatDistance;
int GameplayParams::WastedActionPenalty;
int GameplayParams::CorpseBitesDuration;
float GameplayParams::EatCorpseLifeGained;
float GameplayParams::EatPlantLifeGained;
float GameplayParams::AttackDamage;
float GameplayParams::LifeLostPerTurn;
float GameplayParams::StartingLife;