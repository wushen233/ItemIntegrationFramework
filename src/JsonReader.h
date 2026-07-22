#pragma once
#include "pch.h"

namespace IIF::JsonReader
{
	struct DataSource {
		std::string Type;
		std::string ID;
		int Required{ 0 };
		float Offset{ 0.0f };     // 在乘 Multiplier 之前先加到原始值上：(raw + Offset) * Multiplier
		float Multiplier{ 1.0f };
		std::string Suffix;
		bool active{ false };
	};

	struct BoxConfig {
		std::string tag;
		bool isIcon{ false };
		std::string value;
		std::string state{ "normal" }; 
		std::string align;
		DataSource dataSource;
		bool active{ false }; 
	};

	struct ResultBlock {
		std::string value;
		std::string tag;
		std::string state{ "normal" };
		std::string align;
		
		int hideDifference{ -1 };  
		int invertDiffColor{ -1 }; 
		
		bool isIcon{ false };
		DataSource dataSource;

		float fillPct{ -1.0f };
		float shieldPct{ 0.0f };
		std::uint32_t fillColor{ 0 };
		int showBar{ -1 };
		int showValue{ -1 };
		std::string valueText;
		std::string valueAlign;
		std::uint32_t valueColor{ 0 };
		int valueStandard{ -1 };   

		BoxConfig leftBox;
		BoxConfig rightBox;
		bool hasContent{ false };
	};

	struct JsonRule {
		std::string conditionType; 
		std::string matchType{ "OR" }; 
		std::vector<std::string> conditionIDs;
		
		std::unordered_map<std::string, ResultBlock> valuesMapping;
		ResultBlock result;

		std::string originPath;

		std::string id;
		int priority{ 800 };
		int displayType{ 2 };
		bool hasBackground{ false };
		std::uint32_t backgroundColor{ 0 };
		std::string titleText{ "" }; 
		bool highlightLabel{ false };
		
		std::string state{ "normal" }; 

	std::vector<std::string> anchorTargets; 
	std::vector<std::string> anchorModes;
	std::string sortValueFrom{ "" };

		int hideDifference{ -1 };  
		int invertDiffColor{ -1 }; 
		
		float fillPct{ -1.0f };
		float shieldPct{ 0.0f };
		std::uint32_t fillColor{ 0 };
		bool showBar{ true };
		bool showValue{ false };
		std::string valueText;
		std::string valueAlign;
		std::uint32_t valueColor{ 0 };
		bool valueStandard{ false };   

		BoxConfig globalLeftBox;
		BoxConfig globalRightBox;
	};

	extern std::vector<JsonRule> g_rules;

	struct CPPOverride {
		std::string id;

		int defaultPriority{ 800 };
		std::vector<std::string> defaultAnchorTargets;
		std::vector<std::string> defaultAnchorModes;
		
		int userPriority{ 800 };
		std::vector<std::string> userAnchorTargets;
		std::vector<std::string> userAnchorModes;
		
		bool isOverridden{ false }; 
		bool isRegistered{ false }; 
	};
	
	extern std::map<std::string, CPPOverride> g_cppOverrides;

	void LoadConfigs();
	void SaveConfigs();
	void EvaluateItemRules(RE::TESForm* a_form, RE::BGSInventoryItem* a_item, RE::TBO_InstanceData* a_instance, std::uint32_t a_stackID);
}