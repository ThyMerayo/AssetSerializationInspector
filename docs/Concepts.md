# Concepts

## Understanding Serialized Defaults

A property not appearing in the serialized payload does **not** necessarily mean that the property does not exist.

Unreal frequently uses delta serialization. If a property matches its default value, it may be omitted from the tagged property stream entirely.

This is important when comparing two versions of an asset.

For example:

```text
Old:
    TestFloat not serialized

New:
    TestFloat = 10.0
```

should not be interpreted as:

```text
Property added
```

Instead, it more likely means:

```text
Old:
    <not serialized; likely default>

New:
    10.0
```

The diff system therefore matches properties semantically instead of assuming the serialized field lists must have identical layouts.

## Native and Undecoded Data

Not all UObject serialization is reflected tagged-property serialization.

Native classes can serialize custom data before, after, or instead of reflected properties.

When the plugin cannot confidently map a byte range to a known field, it is reported explicitly as:

```text
<native / undecoded>
```

The plugin should prefer an honest unknown region over guessing and producing misleading field attribution.

As support for more Unreal serialization formats is added, these regions can gradually become more specific.

## Example

A monitored Blueprint that repeatedly changes during otherwise ordinary saves might produce:

```text
BP_Box.uasset

Save Analysis
    Result: Semantic and native/undecoded changes

    Meaningful changes
        TestFloat
            1.0 -> 2.0

        LightingGuid
            3c4d... -> 9a21...

    Layout / serialization
        Default__BP_Box_C
            payload unchanged but moved by +16 bytes
            likely cause: preceding serialized data grew by 16 bytes

    Unexplained
        8 native/undecoded changed bytes

Repeated Save Analysis
    LightingGuid
        Changed: 6 / 6 saves
        Pattern: Continuously changing

        15:22:14    3c4d... -> 9a21...
        15:23:01    9a21... -> f140...
        15:24:18    f140... -> 442a...
```

The intent is to make it possible to move from "the file changed" to a specific semantic explanation, and then trace that explanation all the way back to the exact serialized bytes.
