

from pathlib import Path
import shutil

SRC = Path.home() / "jtmOs"
DST = Path.home() / "jtmOs-repo"


if DST.exists():
    shutil.rmtree(DST)

DST.mkdir(parents=True)


EXCLUDED_EXTENSIONS = {
    ".o",
    ".elf",
    ".iso",
    ".img",
    ".fd",
    ".raw",
    ".log",
}

EXCLUDED_NAMES = {
    ".git",
}

EXCLUDED_DIRS = {
    "build",
    "dist",
    "tmp",
    "__pycache__",
}

def should_copy(path):
    name = path.name

    
    if path.is_dir() and name in EXCLUDED_DIRS:
        return False

    
    if ".bak" in name:
        return False

    if ".before-" in name:
        return False

    
    if path.is_file() and path.suffix in EXCLUDED_EXTENSIONS:
        return False

    
    if name in EXCLUDED_NAMES:
        return False

    return True


def copy_tree(src, dst):
    for item in src.iterdir():
        if not should_copy(item):
            continue

        target = dst / item.name

        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            copy_tree(item, target)

        elif item.is_file():
            shutil.copy2(item, target)


copy_tree(SRC, DST)

print(f"Repo préparé : {DST}")
print("\nContenu :")

for path in sorted(DST.rglob("*")):
    relative = path.relative_to(DST)

    if path.is_dir():
        print(f"  [DIR]  {relative}/")
    else:
        print(f"  [FILE] {relative}")
