#pragma once
#include <unordered_map>
#include ""
// Base interface for the individuals
// Both the ones for evolution and for serialization derive from here
class BaseIndividual
{
	// Does 4 steps
	//    1) Load the sensors from the world
	//    2) Input that values to the network and compute actions probabilities
	//    3) Select the action to do at random based on the probabilities output by the network
	//	  4) Calls the selected action
	virtual void DecideAndExecute(void * World, const class Population*) = 0;

	// "Lower level" version of the decide functionality
	// It expects as input the values for the sensors (as an id->value map) and returns the action ID
	virtual int DecideAction(const unordered_map<int, float> SensorsValues) = 0;

	// Reset the state and set fitness to -1
	// Also flush the neural network
	// Should be called before evaluating fitness
	virtual void Reset() = 0;

	// Getters
	virtual void * GetState() = 0;
	virtual const unordered_map<int, Parameter>& GetParameters() = 0;
	virtual const Mo

};