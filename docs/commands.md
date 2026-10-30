# Command Model

Every change to a document is a command. The command stack applies it, records it, and can undo and redo it. The UI, importers, and scripts all use the same path.

## Interface

```cpp
template <typename Doc>
class Command {
public:
    virtual ~Command() = default;
    virtual void apply(Doc& doc) = 0;     // must be deterministic
    virtual void undo(Doc& doc) = 0;      // exact inverse of apply
    virtual std::string label() const = 0;  // e.g. "Set Boil Time"
    // Absorb `next`, which has already been applied. Return true if merged.
    virtual bool mergeWith(const Command<Doc>& next) { return false; }
};
```

`CommandStack<Doc>` provides `execute`, `undo`, `redo`, `canUndo`, and `canRedo`. `CompositeCommand<Doc>` groups several commands into one undo step (for example, importing a recipe, or changing a style and rescaling its ingredients).

### Grouping edits after the fact

A front end that makes several ordinary edits in a row, such as building a recipe from a template, can still make them one undo step. Take `undoDepth()`, call `seal()` so the first edit does not merge into the step before it, make the edits, then call `groupSince(depth, label)` to join them. `rollbackTo(doc, depth)` undoes and forgets them instead, for when one of the edits fails. The C facade exposes this as `kl_group_begin`, `kl_group_end`, and `kl_group_cancel`. Groups do not nest.

## Rules

1. **Commands store values, not references.** A command holds IDs and the before/after values of the fields it touches. It never holds a pointer or an index into a container.
2. **`undo` is the exact inverse of `apply`.** Store the previous value at apply time, not at construction time, so commands stay correct after earlier undos.
3. **`apply` runs before the command is recorded.** If it throws, nothing is recorded and the document is unchanged.
4. **New edits clear redo history.**
5. **Coalesce continuous edits.** Typing in a field or dragging a slider should produce one undo step. Implement `mergeWith` for the same field on the same entity; keep the original `before` value and take the new `after`.
6. **Composite commands undo in reverse order.**
7. **No UI types in commands.** Commands take the document and nothing else.
8. **Labels are user-facing and localizable.** Front ends show `label()` in menus: "Undo Set Boil Time".

## Example: set a field

```cpp
class SetField : public Command<Recipe> {
public:
    SetField(EntityId id, Field field, Quantity after)
        : id_(id), field_(field), after_(std::move(after)) {}

    void apply(Recipe& r) override {
        before_ = r.get(id_, field_);   // capture at apply time
        r.set(id_, field_, after_);
    }
    void undo(Recipe& r) override { r.set(id_, field_, before_); }
    std::string label() const override { return "Set " + fieldName(field_); }

    bool mergeWith(const Command<Recipe>& next) override {
        auto* n = dynamic_cast<const SetField*>(&next);
        if (!n || n->id_ != id_ || n->field_ != field_) return false;
        after_ = n->after_;
        return true;
    }

private:
    EntityId id_;
    Field field_;
    Quantity after_;
    Quantity before_;
};
```

## Persistence and sync (later)

Commands are the natural unit for a change log. Keep each command's data serializable (IDs, field identifiers, and exact quantities as decimal strings) so it can be written to SQLite and replayed later. Don't add that until a feature needs it.
