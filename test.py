from conans import tools

repo = tools.Git(".")

for line in repo.run("log --oneline --no-decorate").splitlines():
    commit_id, found, message = line.partition(" ")
    assert found
    if message.startswith("import from") and qt:
        print(commit_id, line)
