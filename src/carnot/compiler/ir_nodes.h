#pragma once
#include <pypa/ast/ast.hh>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/carnot/plan/dag.h"
#include "src/carnot/plan/operators.h"
#include "src/common/statusor.h"

namespace pl {
namespace carnot {
namespace compiler {

class IRNode;
using IRNodePtr = std::unique_ptr<IRNode>;

/**
 * IR contains the intermediate representation of the query
 * before compiling into the logical plan.
 */
class IR {
 public:
  /**
   * @brief Node factory that adds a node to the list,
   * updates an id, then returns a pointer to manipulate.
   *
   * The object will be owned by the IR object that created it.
   *
   * @tparam TOperator the type of the operator.
   * @return StatusOr<TOperator *> - the node will be owned
   * by this IR object.
   */
  template <typename TOperator>
  StatusOr<TOperator*> MakeNode() {
    auto id = id_node_map_.size();
    auto node = std::make_unique<TOperator>(id);
    dag_.AddNode(node->id());
    node->SetGraphPtr(this);
    TOperator* raw = node.get();
    id_node_map_.emplace(node->id(), std::move(node));
    nodes_.push_back(raw);
    return raw;
  }

  Status AddEdge(int64_t parent, int64_t child);
  Status AddEdge(IRNode* parent, IRNode* child);
  plan::DAG& dag() { return dag_; }
  std::string DebugString();
  std::vector<IRNode*> Nodes() const { return nodes_; }
  IRNode* Get(int64_t id) const { return id_node_map_.at(id).get(); }

 private:
  plan::DAG dag_;
  std::unordered_map<int64_t, IRNodePtr> id_node_map_;
  std::vector<IRNode*> nodes_;
};

/**
 * @brief Node class for the IR.
 *
 * Each Operator that overlaps IR and LogicalPlan can notify the compiler by returning true in the
 * overloaded HasLogicalRepr method.
 */
class IRNode {
 public:
  IRNode() = delete;
  explicit IRNode(int64_t id) : id_(id) {}
  virtual ~IRNode() = default;
  /**
   * @return whether or not the node has a logical representation.
   */
  virtual bool HasLogicalRepr() const = 0;
  void SetLineCol(int64_t line, int64_t col);
  virtual std::string DebugString(int64_t depth) const = 0;
  /**
   * @brief Set the pointer to the graph.
   * The pointer is passed in by the Node factory of the graph
   * (see IR::MakeNode) so that we can add edges between this
   * object and any other objects created later on.
   *
   * @param graph_ptr : pointer to the graph object.
   */
  void SetGraphPtr(IR* graph_ptr) { graph_ptr_ = graph_ptr; }
  // Returns the ID of the operator.
  int64_t id() const { return id_; }
  IR* graph_ptr() { return graph_ptr_; }

 private:
  int64_t id_;
  // line and column where the parser read the data for this node.
  // used for highlighting errors in queries.
  int64_t line_;
  int64_t col_;
  IR* graph_ptr_;
};

/**
 * @brief The MemorySourceIR is a dual logical plan
 * and IR node operator. It inherits from both classes
 *
 * TODO(philkuz) Do we make the IR operators that do have a logical representation
 * inherit from those logical operators? There's not too
 * much added value to do so and we could just make a method that returns the Logical Plan
 * node.
 */
class MemorySourceIR : public IRNode {
 public:
  MemorySourceIR() = delete;
  explicit MemorySourceIR(int64_t id) : IRNode(id) {}
  Status Init(IRNode* table_node, IRNode* select);
  bool HasLogicalRepr() const override;
  std::string DebugString(int64_t depth) const override;

 private:
  IRNode* table_node_;
  IRNode* select_;
};

/**
 * @brief The RangeIR describe the range()
 * operator, which is combined with a Source
 * when converted to the Logical Plan.
 *
 */
class RangeIR : public IRNode {
 public:
  RangeIR() = delete;
  explicit RangeIR(int64_t id) : IRNode(id) {}
  Status Init(IRNode* parent, IRNode* time_repr);
  bool HasLogicalRepr() const override;
  std::string DebugString(int64_t depth) const override;

 private:
  IRNode* time_repr_;
  IRNode* parent_;
};

/**
 * @brief StringIR wraps around the String AST node
 * and only contains the value of that string.
 *
 */
class StringIR : public IRNode {
 public:
  StringIR() = delete;
  explicit StringIR(int64_t id) : IRNode(id) {}
  Status Init(const std::string str);
  bool HasLogicalRepr() const override;
  std::string str() const { return str_; }
  std::string DebugString(int64_t depth) const override;

 private:
  std::string str_;
};

/**
 * @brief ListIR wraps around lists. Will maintain a
 * vector of pointers to the contained nodes in the
 * list.
 *
 */
class ListIR : public IRNode {
 public:
  ListIR() = delete;
  explicit ListIR(int64_t id) : IRNode(id) {}
  bool HasLogicalRepr() const override;
  Status AddListItem(IRNode* node);
  std::string DebugString(int64_t depth) const override;

 private:
  std::vector<IRNode*> children;
};

/**
 * @brief IR representation for a Lambda
 * function. Should contain an expected
 * Relation based on which columns are called
 * within the contained relation.
 *
 * TODO(philkuz) maybe we move the Relation dependency
 * into a map or agg node?
 */
class LambdaIR : public IRNode {
 public:
  LambdaIR() = delete;
  explicit LambdaIR(int64_t id) : IRNode(id) {}
  Status Init();
  bool HasLogicalRepr() const override;
  std::string DebugString(int64_t depth) const override;

 private:
  plan::Relation expected_relation_;
  IRNode* body;
};

}  // namespace compiler
}  // namespace carnot
}  // namespace pl
