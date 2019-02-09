#include <iostream>
#include "PreyPredator.h"
#include "../../Core/Config.h"
#include <string>
#include <map>

using namespace agio;
using namespace std;

// Need to define them somewhere
int WorldSizeX;
int WorldSizeY;
float FoodScoreGain;
float KillScoreGain;
float DeathPenalty;
int FoodCellCount;
int MaxSimulationSteps;
int SimulationSize;
int PopSizeMultiplier;
int PopulationSize;
int GenerationsCount;
float LifeLostPerTurn;
float BorderPenalty;
float WastedActionPenalty;
string SerializationFile;

int main(int argc, char *argv[])
{
	string cfg_path = "../src/Tests/PreyPredator/Config.cfg";
	if (argc > 3)
		cfg_path = argv[1];

	ConfigLoader loader(cfg_path);

	loader.LoadValue(WorldSizeX, "WorldSizeX");
	loader.LoadValue(WorldSizeY, "WorldSizeY");
	loader.LoadValue(FoodScoreGain, "FoodScoreGain");
	loader.LoadValue(KillScoreGain, "KillScoreGain");
	loader.LoadValue(DeathPenalty, "DeathPenalty");
	float food_proportion;
	loader.LoadValue(food_proportion, "FoodProportion");
	FoodCellCount = WorldSizeX * WorldSizeY*food_proportion;
	loader.LoadValue(MaxSimulationSteps, "MaxSimulationSteps");
	loader.LoadValue(SimulationSize, "SimulationSize");
	loader.LoadValue(PopSizeMultiplier, "PopSizeMultiplier");
	PopulationSize = PopSizeMultiplier * SimulationSize;
	loader.LoadValue(GenerationsCount, "GenerationsCount");
	loader.LoadValue(LifeLostPerTurn, "LifeLostPerTurn");
	loader.LoadValue(BorderPenalty, "BorderPenalty");
	loader.LoadValue(WastedActionPenalty, "WastedActionPenalty");
	loader.LoadValue(SerializationFile, "SerializationFile");

	Settings::LoadFromFile(loader);

    cout << "1 - Run Evolution" << endl;
    cout << "2 - Run Simulation" << endl;
    cout << "Select an option:";

    int op;
    cin >> op;
	if (op == 1)
	{
		auto final_pop = runEvolution();

		// Assume that if some parameter was passed, the second one it's the name of the output file for the parametric config
		if (argc > 3)
			final_pop.SaveRegistryReport(argv[2]);
	}
    else if (op == 2)
        runSimulation();

	return 0;
}