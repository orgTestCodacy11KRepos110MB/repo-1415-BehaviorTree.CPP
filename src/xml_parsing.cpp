#include "behavior_tree_core/xml_parsing.h"
#include <functional>

namespace BT
{
using namespace tinyxml2;

void XMLParser::loadFromFile(const std::string& filename)
{
    XMLError err = doc_.LoadFile(filename.c_str());

    if (err)
    {
        char buffer[200];
        sprintf(buffer, "Error parsing the XML: %s", XMLDocument::ErrorIDToName(err));
        throw std::runtime_error(buffer);
    }
}

void XMLParser::loadFromText(const std::string& xml_text)
{
    XMLError err = doc_.Parse(xml_text.c_str(), xml_text.size());

    if (err)
    {
        char buffer[200];
        sprintf(buffer, "Error parsing the XML: %s", XMLDocument::ErrorIDToName(err));
        throw std::runtime_error(buffer);
    }
}

bool XMLParser::verifyXML(std::vector<std::string>& error_messages) const
{
    error_messages.clear();

    if (doc_.Error())
    {
        error_messages.emplace_back("The XML was not correctly loaded");
        return false;
    }
    bool is_valid = true;

    //-------- Helper functions (lambdas) -----------------
    auto strEqual = [](const char* str1, const char* str2) -> bool { return strcmp(str1, str2) == 0; };

    auto AppendError = [&](int line_num, const char* text) {
        char buffer[256];
        sprintf(buffer, "Error at line %d: -> %s", line_num, text);
        error_messages.emplace_back(buffer);
        is_valid = false;
    };

    auto ChildrenCount = [](const XMLElement* parent_node) {
        int count = 0;
        for (auto node = parent_node->FirstChildElement(); node != nullptr; node = node->NextSiblingElement())
        {
            count++;
        }
        return count;
    };

    //-----------------------------

    const XMLElement* xml_root = doc_.RootElement();

    if (!xml_root || !strEqual(xml_root->Name(), "root"))
    {
        error_messages.emplace_back("The XML must have a root node called <root>");
        return false;
    }
    //-------------------------------------------------
    auto meta_root = xml_root->FirstChildElement("TreeNodesModel");
    auto meta_sibling = meta_root ? meta_root->NextSiblingElement("TreeNodesModel") : nullptr;

    if (meta_sibling)
    {
        AppendError(meta_sibling->GetLineNum(), " Only a single node <TreeNodesModel> is supported");
    }
    if (meta_root)
    {
        // not having a MetaModel is not an error. But consider that the
        // Graphical editor needs it.
        for (auto node = xml_root->FirstChildElement(); node != nullptr; node = node->NextSiblingElement())
        {
            const char* name = node->Name();
            if (strEqual(name, "Action") ||
                    strEqual(name, "Decorator") ||
                    strEqual(name, "SubTree") ||
                    strEqual(name, "Condition"))
            {
                const char* ID = node->Attribute("ID");
                if (!ID)
                {
                    AppendError(node->GetLineNum(), "Error at line %d: -> The attribute [ID] is mandatory");
                }
                for (auto param_node = xml_root->FirstChildElement("Parameter"); param_node != nullptr;
                     param_node = param_node->NextSiblingElement("Parameter"))
                {
                    const char* label = node->Attribute("label");
                    const char* type = node->Attribute("type");
                    if (!label || !type)
                    {
                        AppendError(node->GetLineNum(), "The node <Parameter> requires the attributes [type] and "
                                                        "[label]");
                    }
                }
            }
        }
    }

    //-------------------------------------------------

    // function to be called recursively
    std::function<void(const XMLElement*)> recursiveStep;

    recursiveStep = [&](const XMLElement* node) {
        const int children_count = ChildrenCount(node);
        const char* name = node->Name();
        if (strEqual(name, "Decorator"))
        {
            if (children_count != 1)
            {
                AppendError(node->GetLineNum(), "The node <Decorator> must have exactly 1 child");
            }
            if (!node->Attribute("ID"))
            {
                AppendError(node->GetLineNum(), "The node <Decorator> must have the attribute [ID]");
            }
        }
        else if (strEqual(name, "Action"))
        {
            if (children_count != 0)
            {
                AppendError(node->GetLineNum(), "The node <Action> must not have any child");
            }
            if (!node->Attribute("ID"))
            {
                AppendError(node->GetLineNum(), "The node <Action> must have the attribute [ID]");
            }
        }
        else if (strEqual(name, "Condition"))
        {
            if (children_count != 0)
            {
                AppendError(node->GetLineNum(), "The node <Condition> must not have any child");
            }
            if (!node->Attribute("ID"))
            {
                AppendError(node->GetLineNum(), "The node <Condition> must have the attribute [ID]");
            }
        }
        else if (strEqual(name, "Sequence") || strEqual(name, "SequenceStar") || strEqual(name, "Fallback") ||
                 strEqual(name, "FallbackStar"))
        {
            if (children_count == 0)
            {
                AppendError(node->GetLineNum(), "A Control node must have at least 1 child");
            }
        }
        else if (strEqual(name, "SubTree"))
        {
            if (children_count > 0)
            {
                AppendError(node->GetLineNum(), "The <SubTree> node must have no children");
            }
            if (!node->Attribute("ID"))
            {
                AppendError(node->GetLineNum(), "The node <SubTree> must have the attribute [ID]");
            }
        }
        else
        {
            AppendError(node->GetLineNum(), "Node not recognized");
        }
        //recursion
        for (auto child = node->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
        {
            recursiveStep(child);
        }
    };

    for (auto bt_root = xml_root->FirstChildElement("BehaviorTree"); bt_root != nullptr;
         bt_root = bt_root->NextSiblingElement("BehaviorTree"))
    {
        if (ChildrenCount(bt_root) != 1)
        {
            AppendError(bt_root->GetLineNum(), "The node <BehaviorTree> must have exactly 1 child");
        }
        else
        {
            recursiveStep(bt_root->FirstChildElement());
        }
    }
    return is_valid;
}

TreeNodePtr XMLParser::instantiateTree(const BehaviorTreeFactory& factory, std::vector<TreeNodePtr>& nodes)
{
    std::vector<std::string> error_messages;
    this->verifyXML(error_messages);

    if (error_messages.size() > 0)
    {
        for (const std::string& str : error_messages)
        {
            std::cerr << str << std::endl;
        }
        throw std::runtime_error("verifyXML failed");
    }

    //--------------------------------------
    XMLElement* xml_root = doc_.RootElement();
    const std::string main_tree_ID = xml_root->Attribute("main_tree_to_execute");

    std::map<std::string, XMLElement*> bt_roots;

    for (auto node = xml_root->FirstChildElement("BehaviorTree"); node != nullptr;
         node = node->NextSiblingElement("BehaviorTree"))
    {
        bt_roots[node->Attribute("ID")] = node;
    }

    //--------------------------------------
    NodeBuilder<TreeNodePtr> node_builder = [&](const std::string& ID, const std::string& name,
                                                const NodeParameters& params, TreeNodePtr parent) -> TreeNodePtr {
        TreeNodePtr child_node = factory.instantiateTreeNode(ID, name, params);
        nodes.push_back(child_node);
        if (parent)
        {
            ControlNode* control_parent = dynamic_cast<ControlNode*>(parent.get());
            if (control_parent)
            {
                control_parent->addChild(child_node.get());
            }
            DecoratorNode* decorator_parent = dynamic_cast<DecoratorNode*>(parent.get());
            if (decorator_parent)
            {
                decorator_parent->setChild(child_node.get());
            }
        }
        DecoratorSubtreeNode* subtree_node = dynamic_cast<DecoratorSubtreeNode*>(child_node.get());

        if (subtree_node)
        {
            auto subtree_elem = bt_roots[name]->FirstChildElement();
            treeParsing<TreeNodePtr>(subtree_elem, node_builder, nodes, child_node);
        }
        return child_node;
    };
    //--------------------------------------

    auto root_element = bt_roots[main_tree_ID]->FirstChildElement();
    return treeParsing<TreeNodePtr>(root_element, node_builder, nodes, TreeNodePtr());
}
}