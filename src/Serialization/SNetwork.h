#pragma once

#include <vector>
#include <math.h>

#include "../../NEAT/include/network.h"
#include "../../NEAT/include/nnode.h"

// Forward declarations
class SNode;
class SNetwork

enum class NodeType{
    NEURON = 0,
    SENSOR = 1
};

inline double sigmoid(double value) {
    return (1/(1+(exp(-(4.924273 * value))))); // look at neat.cpp line 482
}

class SLink {
    friend SNode;
    friend SNetwork;

    double weight;
    SNode *in_node;

};

class SNode{
    NodeType type;
    double activation;

    std::vector<SLink*> incoming;
    std::vector<SLink*> outgoing;

    SNode();
    SNode(NEAT::NNode* node);
private:
    friend SNetwork;
    friend SLink;

    double activesum;
    bool active_flag;
    int activation_count;

    double getActiveOut();
    void flushback();
};

class SNetwork {
public:
    void flush();
    bool activate();
    void load_sensors(const std::vector<double> &sensorsValues);

    SNetwork();
    SNetwork(NEAT::Network *network);
private:
    std::vector<SNode*> all_nodes;
    std::vector<SNode*> inputs;
    std::vector<SNode*> outputs;

    bool outputsOff();
};