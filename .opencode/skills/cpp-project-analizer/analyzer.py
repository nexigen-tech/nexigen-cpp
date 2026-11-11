import os
import sys
import subprocess
import json
import re

# Правило навыка: файлы из верхнеуровневой директории examples
# анализируются и репортятся ОТДЕЛЬНО от основного проекта.
EXAMPLES_DIR = "examples"
CPP_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx")
ALIGNAS_PSEUDO_FUNCTION = re.compile(r"(^|::)alignas(\(|$)")

# Directories that hold generated or vendored copies of third-party sources.
# cloc runs on the repository root, so without this it counts the build trees
# (eleven of them at the time of writing) as project code. Anchored on path
# separators: a directory that merely contains "build" stays included.
CLOC_EXCLUDE_DIRS = r"(^|/)(build[^/]*|html|graphify-out|\.git)(/|$)"


def new_metrics():
    return {
        "files": {"cpp": 0, "headers": 0, "build_systems": 0},
        "lines": {"code": 0, "comments": 0, "blank": 0},
        "complexity": {"total_functions": 0, "high_complexity_functions": [], "avg_complexity": 0},
        "tech_debt": {"todo_count": 0, "fixme_count": 0},
    }


def bucket_for(path, project_path):
    """К какому разделу относится файл: 'examples' или 'project'.

    Критерий — верхнеуровневый сегмент пути относительно корня проекта
    (т.е. <root>/examples/... попадает в examples, всё остальное — в project).
    """
    rel = os.path.normpath(os.path.relpath(path, project_path))
    top = rel.split(os.sep)[0]
    if top == ".." or os.path.isabs(top):
        # Путь вне проекта — определяем по сегментам пути.
        parts = os.path.normpath(path).replace("\\", "/").split("/")
        return "examples" if EXAMPLES_DIR in parts else "project"
    return "examples" if top == EXAMPLES_DIR else "project"


def run_cloc(args):
    """cloc --json по списку аргументов; None, если cloc недоступен."""
    try:
        res = subprocess.run(
            ["cloc", *args, f"--not-match-d={CLOC_EXCLUDE_DIRS}", "--json"],
            capture_output=True,
            text=True,
        )
        if res.returncode == 0:
            return json.loads(res.stdout)
    except Exception:
        pass  # Если cloc не установлен, агент соберет данные встроенными средствами
    return None


def apply_cloc(metrics, cloc_data):
    if not cloc_data:
        return
    for lang in ("C++", "C/C++ Header"):
        if lang not in cloc_data:
            continue
        metrics["lines"]["code"] += cloc_data[lang].get("code", 0)
        metrics["lines"]["comments"] += cloc_data[lang].get("comment", 0)
        metrics["lines"]["blank"] += cloc_data[lang].get("blank", 0)
        if lang == "C++":
            metrics["files"]["cpp"] += cloc_data[lang].get("nFiles", 0)
        else:
            metrics["files"]["headers"] += cloc_data[lang].get("nFiles", 0)


def run_analysis(project_path):
    project = new_metrics()
    examples = new_metrics()
    buckets = {"project": project, "examples": examples}

    # 1. Сбор базовых метрик строк кода через cloc (если доступен):
    #    весь проект целиком + директория examples отдельно;
    #    project = total - examples.
    total_cloc = run_cloc([project_path])
    examples_path = os.path.join(project_path, EXAMPLES_DIR)
    examples_cloc = run_cloc([examples_path]) if os.path.isdir(examples_path) else None
    if total_cloc is not None:
        apply_cloc(project, total_cloc)
        if examples_cloc is not None:
            apply_cloc(examples, examples_cloc)
            for key in ("code", "comments", "blank"):
                project["lines"][key] -= examples["lines"][key]
            for key in ("cpp", "headers"):
                project["files"][key] -= examples["files"][key]

    # 2. Сканирование файлов сборки и техдолга; каждый файл направляется
    #    в свой раздел (project или examples).
    for root, dirs, files in os.walk(project_path):
        # Игнорируем папки сборки и зависимостей
        if any(p in root for p in ["build", ".git", "vcpkg_installed", "node_modules"]):
            continue

        for file in files:
            file_path = os.path.join(root, file)
            bucket = buckets[bucket_for(file_path, project_path)]

            # Считаем файлы сборки
            if file in ["CMakeLists.txt", "Makefile", "conanfile.txt"]:
                bucket["files"]["build_systems"] += 1

            # Ищем TODO и FIXME в C++ файлах
            if file.endswith(('.cpp', '.h', '.hpp', '.cc', '.cxx')):
                try:
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                        bucket["tech_debt"]["todo_count"] += len(re.findall(r'\bTODO\b', content, re.IGNORECASE))
                        bucket["tech_debt"]["fixme_count"] += len(re.findall(r'\bFIXME\b', content, re.IGNORECASE))
                except Exception:
                    pass

    # 3. Использование lizard для подсчета цикломатической сложности функций:
    #    каждая функция попадает в раздел по расположению своего файла.
    complexity_sum = {"project": 0, "examples": 0}
    try:
        import lizard
        try:
            analysis = lizard.analyze([project_path], lans=["cpp"])
            cpp_only = True
        except TypeError:
            # Older lizard releases do not accept `lans`: analyse everything and
            # filter by extension below.
            analysis = lizard.analyze([project_path])
            cpp_only = False
        # Новые версии lizard возвращают генератор FileInformation
        if hasattr(analysis, 'function_list'):
            func_lists = [analysis.function_list]
        else:
            func_lists = [file_info.function_list for file_info in analysis]
        for func_list in func_lists:
            for func in func_list:
                if not cpp_only and not func.filename.endswith(CPP_SUFFIXES):
                    continue
                # `class alignas(64) T { ... }` is parsed as a function named
                # "alignas" whose complexity sums every member; the gate filters
                # it by identifier and so does this report.
                if ALIGNAS_PSEUDO_FUNCTION.search(func.name):
                    continue
                name = bucket_for(func.filename, project_path)
                bucket = buckets[name]
                bucket["complexity"]["total_functions"] += 1
                complexity_sum[name] += func.cyclomatic_complexity
                if func.cyclomatic_complexity > 15:  # Порог высокой сложности
                    bucket["complexity"]["high_complexity_functions"].append({
                        "name": func.name,
                        "complexity": func.cyclomatic_complexity,
                        "filename": func.filename
                    })
    except ImportError:
        pass

    for name, bucket in buckets.items():
        cnt = bucket["complexity"]["total_functions"]
        bucket["complexity"]["avg_complexity"] = (
            round(complexity_sum[name] / cnt, 2) if cnt > 0 else 0
        )

    # 4. Суммарный раздел total = project + examples.
    total = new_metrics()
    for key in ("cpp", "headers", "build_systems"):
        total["files"][key] = project["files"][key] + examples["files"][key]
    for key in ("code", "comments", "blank"):
        total["lines"][key] = project["lines"][key] + examples["lines"][key]
    for key in ("todo_count", "fixme_count"):
        total["tech_debt"][key] = project["tech_debt"][key] + examples["tech_debt"][key]
    total["complexity"]["total_functions"] = (
        project["complexity"]["total_functions"] + examples["complexity"]["total_functions"]
    )
    total["complexity"]["high_complexity_functions"] = (
        project["complexity"]["high_complexity_functions"]
        + examples["complexity"]["high_complexity_functions"]
    )
    total_sum = complexity_sum["project"] + complexity_sum["examples"]
    total_cnt = total["complexity"]["total_functions"]
    total["complexity"]["avg_complexity"] = round(total_sum / total_cnt, 2) if total_cnt > 0 else 0

    result = {"project": project, "examples": examples, "total": total}
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "."
    run_analysis(path)
