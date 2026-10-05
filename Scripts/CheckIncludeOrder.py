"""Checks the order of the #include lines of the plugin's C++ files.

The rules (one blank line between blocks):

  * blocks in this order: Engine, other plugins, this plugin's own headers ("local"), external libraries (<...> includes);
  * a block holds one kind of header only, and two blocks of the same kind are one block;
  * in a .cpp, the include of its own header is the first include, alone in its block;
  * a .generated.h include is the last include of the file, alone in its block.

Headers that resolve to a file under Source/<Module>/Public or Private (or beside the file) are local; every other quoted include is
an engine header. Run it from anywhere:

    python Scripts/CheckIncludeOrder.py

It prints the files that break a rule and exits with 1 when there are any. Formatting itself (sorting inside a block) is
clang-format's job: run clang-format on the files afterwards.
"""
import os
import re
import sys

REPOSITORY = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SOURCE = os.path.join(REPOSITORY, 'Source')

INCLUDE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]')
ORDER = {'engine': 1, 'plugin': 2, 'local': 3, 'external': 4}


def local_headers():
    result = set()
    for module in os.listdir(SOURCE):
        for root in ('Public', 'Private'):
            base = os.path.join(SOURCE, module, root)
            for folder, _, files in os.walk(base):
                for name in files:
                    result.add(os.path.relpath(os.path.join(folder, name), base).replace('\\', '/'))
    return result


LOCAL = local_headers()


def category(path, bracket, folder):
    if path.endswith('.generated.h'):
        return 'generated'
    if bracket == '<':
        return 'external'
    if path in LOCAL or os.path.exists(os.path.join(folder, path)):
        return 'local'
    return 'engine'


def check(path):
    text = open(path, 'rb').read().decode('latin-1').replace('\r\n', '\n')
    lines = text.split('\n')
    stem = os.path.splitext(os.path.basename(path))[0]
    is_cpp = path.endswith('.cpp')
    folder = os.path.dirname(path)

    # Runs of include lines. A blank line or any other line ends a run; the gap before a run says whether only blank lines separate it
    # from the previous one (a conditional such as #if between two runs is allowed to keep them apart).
    groups = []
    only_blank_before = []
    current = None
    last_include = -1
    for number, line in enumerate(lines):
        match = INCLUDE.match(line)
        if not match:
            current = None
            continue
        if current is None:
            current = []
            groups.append(current)
            only_blank_before.append(last_include >= 0 and all(l.strip() == '' for l in lines[last_include + 1:number]))
        current.append((match.group(2), category(match.group(2), match.group(1), folder)))
        last_include = number

    problems = []
    if not groups:
        return problems

    own = None
    if is_cpp:
        for group in groups:
            for header, kind in group:
                if kind == 'local' and os.path.splitext(os.path.basename(header))[0] == stem:
                    own = header
        if own is not None and not (len(groups[0]) == 1 and groups[0][0][0] == own):
            problems.append('the header of the .cpp (%s) is not the first include, alone in its block' % own)

    def kinds(group):
        return set(kind for header, kind in group if header != own)

    highest = 0
    for index, group in enumerate(groups):
        if own is not None and len(group) == 1 and group[0][0] == own:
            continue
        present = kinds(group)
        present.discard('generated')
        if len(present) > 1:
            problems.append('block %d mixes %s' % (index + 1, ', '.join(sorted(present))))
        for kind in present:
            if ORDER[kind] < highest:
                problems.append('block %d (%s) comes after a block of a later kind' % (index + 1, kind))
            highest = max(highest, ORDER[kind])

    for index in range(1, len(groups)):
        previous, current_kinds = kinds(groups[index - 1]), kinds(groups[index])
        previous_is_own = own is not None and len(groups[index - 1]) == 1 and groups[index - 1][0][0] == own
        if only_blank_before[index] and len(previous) == 1 and previous == current_kinds and not previous_is_own and 'generated' not in previous:
            problems.append('blocks %d and %d are both %s, split by blank lines only' % (index, index + 1, next(iter(previous))))

    flat = [item for group in groups for item in group]
    for item in [i for i in flat if i[1] == 'generated']:
        group = next(g for g in groups if item in g)
        if len(group) != 1 or flat[-1] != item:
            problems.append('%s is not the last include, alone in its block' % item[0])

    return problems


def main():
    failed = 0
    for folder, _, files in os.walk(SOURCE):
        for name in sorted(files):
            if name.endswith(('.cpp', '.h')):
                path = os.path.join(folder, name)
                problems = check(path)
                if problems:
                    failed += 1
                    print(os.path.relpath(path, REPOSITORY).replace('\\', '/'))
                    for problem in problems:
                        print('   - ' + problem)

    print('%d file(s) break the include order.' % failed if failed else 'Include order is fine.')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
