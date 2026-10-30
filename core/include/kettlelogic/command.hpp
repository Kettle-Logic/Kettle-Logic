#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kettlelogic {

// A reversible edit to a document of type Doc. See docs/commands.md.
template <typename Doc>
class Command {
public:
    virtual ~Command() = default;

    // Must be deterministic. Capture "before" state here, not in the constructor.
    virtual void apply(Doc& doc) = 0;

    // Exact inverse of apply.
    virtual void undo(Doc& doc) = 0;

    // User-facing, for menus: "Undo Set Boil Time".
    virtual std::string label() const = 0;

    // Called on the command at the top of the undo stack, with `next` already
    // applied. Return true to absorb it; the stack then drops `next`.
    virtual bool mergeWith(const Command<Doc>& /*next*/) { return false; }
};

// Several commands as one undo step. Applies in order, undoes in reverse.
template <typename Doc>
class CompositeCommand : public Command<Doc> {
public:
    using Part = std::unique_ptr<Command<Doc>>;

    CompositeCommand(std::string label, std::vector<Part> parts)
        : label_(std::move(label)), parts_(std::move(parts)) {}

    void apply(Doc& doc) override {
        std::size_t applied = 0;
        try {
            for (; applied < parts_.size(); ++applied) {
                parts_[applied]->apply(doc);
            }
        } catch (...) {
            // Roll back whatever did apply, then let the failure propagate.
            while (applied > 0) {
                parts_[--applied]->undo(doc);
            }
            throw;
        }
    }

    void undo(Doc& doc) override {
        for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) {
            (*it)->undo(doc);
        }
    }

    std::string label() const override { return label_; }

private:
    std::string label_;
    std::vector<Part> parts_;
};

template <typename Doc>
class CommandStack {
public:
    using Ptr = std::unique_ptr<Command<Doc>>;

    // Applies `command`, then records it. If apply throws, nothing is recorded.
    void execute(Doc& doc, Ptr command) {
        command->apply(doc);
        redo_.clear();
        const bool mayMerge = !sealed_;
        sealed_ = false;
        if (mayMerge && !undo_.empty() && undo_.back()->mergeWith(*command)) {
            return;
        }
        undo_.push_back(std::move(command));
    }

    // Grouping several edits into one undo step: take `undoDepth()`, seal, execute the edits, then
    // call `groupSince` with the depth, or `rollbackTo` to take them back.

    // How many undo steps are recorded.
    std::size_t undoDepth() const { return undo_.size(); }

    // Stops the next executed command from merging into the one before it, so a group starts clean.
    void seal() { sealed_ = true; }

    // Joins the steps recorded after the stack held `depth` into one step called `label`. Undo then
    // reverses all of them, and redo applies them again in order.
    void groupSince(std::size_t depth, std::string label) {
        if (undo_.size() <= depth) {
            return;
        }
        std::vector<typename CompositeCommand<Doc>::Part> parts;
        for (std::size_t i = depth; i < undo_.size(); ++i) {
            parts.push_back(std::move(undo_[i]));
        }
        undo_.resize(depth);
        undo_.push_back(std::make_unique<CompositeCommand<Doc>>(std::move(label), std::move(parts)));
    }

    // Undoes the steps recorded after the stack held `depth`, and forgets them, leaving no redo.
    void rollbackTo(Doc& doc, std::size_t depth) {
        while (undo_.size() > depth) {
            Ptr command = std::move(undo_.back());
            undo_.pop_back();
            command->undo(doc);
        }
        redo_.clear();
    }

    bool undo(Doc& doc) {
        if (undo_.empty()) {
            return false;
        }
        Ptr command = std::move(undo_.back());
        undo_.pop_back();
        command->undo(doc);
        redo_.push_back(std::move(command));
        return true;
    }

    bool redo(Doc& doc) {
        if (redo_.empty()) {
            return false;
        }
        Ptr command = std::move(redo_.back());
        redo_.pop_back();
        command->apply(doc);
        undo_.push_back(std::move(command));
        return true;
    }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // Labels for menu items, or empty when there is nothing to undo or redo.
    std::string undoLabel() const { return undo_.empty() ? std::string{} : undo_.back()->label(); }
    std::string redoLabel() const { return redo_.empty() ? std::string{} : redo_.back()->label(); }

private:
    std::vector<Ptr> undo_;
    std::vector<Ptr> redo_;
    bool sealed_ = false;
};

}  // namespace kettlelogic
