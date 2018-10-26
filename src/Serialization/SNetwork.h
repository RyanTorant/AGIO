#pragma once

#include <vector>
#include <math.h>
#include <unordered_map>

//#include "../../NEAT/include/network.h"
//#include "../../NEAT/include/nnode.h"

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>

// Forward declarations
namespace NEAT
{
	class Network;
	class NNode;
	class Link;
}

namespace agio
{
	// Forward declarations
	class SNode;
	class SNetwork;

	enum class NodeType 
	{
		NEURON = 0,
		SENSOR = 1
	};

	inline double sigmoid(double value) 
	{
		return (1 / (1 + (exp(-(4.924273 * value))))); // look at neat.cpp line 482
	}

	class SLink 
	{
		// Ref counter used to prevent double deletions
		//	SLinks are either loaded from file, where there's no problem with duplicates, or created from findLink
		// on the second case, there may be duplicates.
		int RefCount;
	public:
		friend SNode;
		friend SNetwork;

		double weight;
		SNode *in_node;
		SNode *out_node;

		SLink();
		SLink(double weight, SNode *in_node, SNode *out_node);

		friend class boost::serialization::access;
		template<class Archive>
		void serialize(Archive &ar, const unsigned int version)
		{
			ar & weight;
			ar & in_node;
			ar & out_node;
		}
	};

	class SNode 
	{
	public:
		NodeType type;
		double activation;

		std::vector<SLink*> incoming;
		std::vector<SLink*> outgoing;

		SNode();
		SNode(NodeType type);
	private:
		friend SNetwork;
		friend SLink;

		double activesum;
		bool active_flag;
		int activation_count;

		double getActiveOut();
		void flushback();

		friend class boost::serialization::access;
		template<class Archive>
		void serialize(Archive &ar, const unsigned int version)
		{
			ar & type;
			ar & activation;
			ar & incoming;
			ar & outgoing;
		}
	};

	class SNetwork
	{
		bool WasMoved;
	public:
		std::vector<SNode*> inputs;
		std::vector<SNode*> outputs;
		std::vector<SNode*> all_nodes;

		void flush();
		bool activate();
		void load_sensors(const std::vector<double> &sensorsValues);

		SNetwork();
		SNetwork(NEAT::Network *network);
		~SNetwork();
		SNetwork(SNetwork&&);
		SNetwork& operator=(SNetwork&&);

		// Creates a new network that's a duplicate of this
		// Can't just make a copy because there are pointers involved
		void Duplicate(SNetwork& CloneTarget) const;
	private:

		// variables used for internal logic
		std::unordered_map<NEAT::Link*, SLink*> linkMap;
		std::unordered_map<NEAT::NNode*, SNode*> nodeMap;

		bool outputsOff();
		SLink* findLink(NEAT::Link *link);

		friend class boost::serialization::access;
		template<class Archive>
		void serialize(Archive &ar, const unsigned int version)
		{
			ar & all_nodes;
			ar & inputs;
			ar & outputs;
		}
	};
}