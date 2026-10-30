#include "check.hpp"

#include "kettlelogic/command.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kettlelogic::test {

namespace {

using kettlelogic::Command;
using kettlelogic::CommandStack;
using kettlelogic::CompositeCommand;

struct Counter {
    int value = 0;
};

class SetValue : public Command<Counter> {
public:
    explicit SetValue(int after) : after_(after) {}

    void apply(Counter& doc) override {
        before_ = doc.value;
        doc.value = after_;
    }
    void undo(Counter& doc) override { doc.value = before_; }
    std::string label() const override { return "Set Value"; }

    bool mergeWith(const Command<Counter>& next) override {
        auto* other = dynamic_cast<const SetValue*>(&next);
        if (other == nullptr) {
            return false;
        }
        after_ = other->after_;
        return true;
    }

private:
    int after_;
    int before_ = 0;
};

void testExecuteUndoRedo() {
    Counter doc;
    CommandStack<Counter> stack;

    stack.execute(doc, std::make_unique<SetValue>(5));
    CHECK(doc.value == 5);

    CHECK(stack.undo(doc));
    CHECK(doc.value == 0);
    CHECK(stack.canRedo());

    CHECK(stack.redo(doc));
    CHECK(doc.value == 5);

    // A new edit clears redo history.
    CHECK(stack.undo(doc));
    stack.execute(doc, std::make_unique<SetValue>(7));
    CHECK(!stack.canRedo());
}

void testGroupSinceMakesOneStep() {
    Counter doc;
    CommandStack<Counter> stack;
    stack.execute(doc, std::make_unique<SetValue>(1));

    const std::size_t depth = stack.undoDepth();
    stack.seal();
    stack.execute(doc, std::make_unique<SetValue>(2));
    stack.execute(doc, std::make_unique<SetValue>(3));
    stack.groupSince(depth, "Group");

    // The group did not merge into the step before it, and it undoes as one.
    CHECK(stack.undoLabel() == "Group");
    CHECK(stack.undo(doc));
    CHECK(doc.value == 1);
    CHECK(stack.undo(doc));
    CHECK(doc.value == 0);

    // Redo applies the group's parts again, in order.
    CHECK(stack.redo(doc));
    CHECK(stack.redo(doc));
    CHECK(doc.value == 3);
}

void testRollbackToTakesGroupBack() {
    Counter doc;
    CommandStack<Counter> stack;
    stack.execute(doc, std::make_unique<SetValue>(1));
    const std::size_t depth = stack.undoDepth();
    stack.seal();
    stack.execute(doc, std::make_unique<SetValue>(2));

    stack.rollbackTo(doc, depth);
    CHECK(doc.value == 1);
    CHECK(!stack.canRedo());
    CHECK(stack.undoDepth() == depth);
}

void testMergeCoalescesEdits() {
    Counter doc;
    CommandStack<Counter> stack;

    stack.execute(doc, std::make_unique<SetValue>(1));
    stack.execute(doc, std::make_unique<SetValue>(2));
    stack.execute(doc, std::make_unique<SetValue>(3));
    CHECK(doc.value == 3);

    // Three keystrokes, one undo step, restoring the value before the first.
    CHECK(stack.undo(doc));
    CHECK(doc.value == 0);
    CHECK(!stack.canUndo());
}

void testCompositeUndoesInReverse() {
    Counter doc;
    CommandStack<Counter> stack;

    std::vector<std::unique_ptr<Command<Counter>>> parts;
    parts.push_back(std::make_unique<SetValue>(4));
    parts.push_back(std::make_unique<SetValue>(9));
    stack.execute(doc, std::make_unique<CompositeCommand<Counter>>("Import", std::move(parts)));
    CHECK(doc.value == 9);

    CHECK(stack.undo(doc));
    CHECK(doc.value == 0);
}

class FailingCommand : public Command<Counter> {
public:
    void apply(Counter& doc) override {
        doc.value += 100;
        throw std::runtime_error("boom");
    }
    void undo(Counter& doc) override { doc.value -= 100; }
    std::string label() const override { return "Failing"; }
};

void testFailedApplyLeavesNothingRecorded() {
    Counter doc;
    CommandStack<Counter> stack;

    CHECK(throws([&] { stack.execute(doc, std::make_unique<FailingCommand>()); }));
    CHECK(!stack.canUndo());
}

void testCompositeRollsBackOnFailure() {
    Counter doc;
    std::vector<std::unique_ptr<Command<Counter>>> parts;
    parts.push_back(std::make_unique<SetValue>(4));
    parts.push_back(std::make_unique<FailingCommand>());

    CompositeCommand<Counter> composite("Partial", std::move(parts));
    CHECK(throws([&] { composite.apply(doc); }));
    CHECK(doc.value == 0);
}

}  // namespace

void runCommandTests() {
    testExecuteUndoRedo();
    testMergeCoalescesEdits();
    testGroupSinceMakesOneStep();
    testRollbackToTakesGroupBack();
    testCompositeUndoesInReverse();
    testFailedApplyLeavesNothingRecorded();
    testCompositeRollsBackOnFailure();
}

}  // namespace kettlelogic::test
