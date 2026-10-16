/*
 * libgit2 "worktree" example - manages linked working trees
 *
 * Written by the libgit2 and a-Shell contributors
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 700
# endif
#endif
#include "common.h"

static int worktree_usage(void)
{
	fprintf(stderr, "usage: lg2 worktree add [-b <new-branch>] <path> [<branch>]\n"
		"       lg2 worktree list\n"
		"       lg2 worktree remove [--force] <path>\n"
		"       lg2 worktree lock <path>\n"
		"       lg2 worktree unlock <path>\n");
	return 1;
}

static int worktree_error(const char *message)
{
	fprintf(stderr, "worktree: %s\n", message);
	return 1;
}

static int worktree_add(git_repository *repo, int argc, char **argv)
{
	git_worktree_add_options opts = GIT_WORKTREE_ADD_OPTIONS_INIT;
	git_worktree *wt = NULL;
	git_reference *ref = NULL;
	git_object *start = NULL;
	const char *new_branch = NULL, *path, *branch = NULL, *base;
	char *name = NULL;
	size_t len;
	int pos = 2, end_options = 0, error = 1;

	if (pos < argc && !strcmp(argv[pos], "-b")) {
		if (++pos == argc)
			return worktree_usage();
		new_branch = argv[pos++];
	}
	if (pos < argc && !strcmp(argv[pos], "--")) {
		end_options = 1;
		pos++;
	}
	if (pos == argc || argc - pos > 2 || (!end_options && argv[pos][0] == '-'))
		return worktree_usage();
	path = argv[pos++];
	if (pos < argc)
		branch = argv[pos];
	len = strlen(path);
	while (len && path[len - 1] == '/')
		len--;
	base = path + len;
	while (base > path && base[-1] != '/')
		base--;
	if (base == path + len)
		return worktree_error("path has no worktree name");
	name = malloc((size_t)(path + len - base) + 1);
	if (!name)
		return worktree_error("out of memory");
	memcpy(name, base, (size_t)(path + len - base));
	name[path + len - base] = '\0';
	if (!strcmp(name, ".") || !strcmp(name, "..")) {
		error = worktree_error("invalid worktree name: '.' and '..' are not allowed");
		goto done;
	}

	if (new_branch) {
		if ((error = git_revparse_single(&start, repo, branch ? branch : "HEAD")) < 0)
			goto done;
		if (git_object_type(start) != GIT_OBJECT_COMMIT) {
			error = worktree_error("start point must be a commit");
			goto done;
		}
		if ((error = git_branch_create(&ref, repo, new_branch, (git_commit *)start, 0)) < 0)
			goto done;
	} else if (branch) {
		if ((error = git_branch_lookup(&ref, repo, branch, GIT_BRANCH_LOCAL)) < 0)
			goto done;
	} else {
		error = git_branch_lookup(&ref, repo, name, GIT_BRANCH_LOCAL);
		if (error != 0 && error != GIT_ENOTFOUND)
			goto done;
	}
	opts.ref = ref;
	error = git_worktree_add(&wt, repo, name, path, &opts);
	/* Like Git, retain a newly created branch if checkout fails. */
done:
	git_worktree_free(wt);
	git_reference_free(ref);
	git_object_free(start);
	free(name);
	return error;
}

static int worktree_print(git_repository *repo, git_worktree *wt)
{
	git_reference *head = NULL;
	const char *oid;
	int error, locked = 0;
	const char *path = git_repository_workdir(repo);

	if (!path)
		path = git_repository_path(repo);
	if (wt && (locked = git_worktree_is_locked(NULL, wt)) < 0)
		return locked;
	if (git_repository_is_bare(repo)) {
		printf("%s (bare)\n", path);
		return 0;
	}
	error = git_repository_head(&head, repo);
	if (error == GIT_EUNBORNBRANCH) {
		printf("%s (unborn)\n", path);
		return 0;
	}
	if (error < 0)
		return error;
	oid = git_oid_tostr_s(git_reference_target(head));
	printf("%s %.7s %s%s%s\n", path, oid,
		git_reference_is_branch(head) ? "[" : "(",
		git_reference_is_branch(head) ? git_reference_shorthand(head) : "detached HEAD",
		git_reference_is_branch(head) ? "]" : ")");
	if (locked)
		printf("  locked\n");
	git_reference_free(head);
	return 0;
}

static int worktree_list(git_repository *repo)
{
	git_strarray names = {0};
	git_worktree_prune_options opts = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
	git_repository *main_repo = NULL, *linked = NULL;
	git_worktree *wt = NULL;
	size_t i;
	int error, locked, prunable;

	if ((error = git_repository_open(&main_repo, git_repository_commondir(repo))) < 0 ||
		(error = worktree_print(main_repo, NULL)) < 0 ||
		(error = git_worktree_list(&names, repo)) < 0)
		goto done;
	for (i = 0; i < names.count; i++) {
		if ((error = git_worktree_lookup(&wt, repo, names.strings[i])) < 0)
			goto done;
		if (git_worktree_validate(wt) < 0) {
			if ((locked = git_worktree_is_locked(NULL, wt)) < 0 ||
				(prunable = git_worktree_is_prunable(wt, &opts)) < 0) {
				error = locked < 0 ? locked : prunable;
				goto done;
			}
			printf("%s (%s)\n", git_worktree_path(wt), prunable ? "prunable" : "invalid");
			if (locked)
				printf("  locked\n");
		} else {
			if ((error = git_repository_open_from_worktree(&linked, wt)) < 0 ||
				(error = worktree_print(linked, wt)) < 0)
				goto done;
		}
		git_repository_free(linked);
		linked = NULL;
		git_worktree_free(wt);
		wt = NULL;
	}
	error = 0;
done:
	git_strarray_dispose(&names);
	git_worktree_free(wt);
	git_repository_free(linked);
	git_repository_free(main_repo);
	return error;
}

static char *worktree_realpath(const char *path)
{
#ifdef _WIN32
	return _fullpath(NULL, path, 0);
#else
	return realpath(path, NULL);
#endif
}

static int worktree_find(git_worktree **out, git_repository *repo, const char *path)
{
	git_repository *target = NULL;
	char *requested = NULL, *recorded = NULL;
	int error;

	if ((error = git_repository_open(&target, path)) < 0)
		return error;
	if (strcmp(git_repository_commondir(repo), git_repository_commondir(target))) {
		error = worktree_error("path belongs to another repository");
		goto done;
	}
	if ((error = git_worktree_open_from_repository(out, target)) < 0) {
		if (git_repository_is_worktree(target))
			error = worktree_error("cannot resolve recorded worktree path; moved worktrees are not supported");
		goto done;
	}
	requested = worktree_realpath(path);
	recorded = worktree_realpath(git_worktree_path(*out));
	if (!requested || !recorded || strcmp(requested, recorded)) {
		git_worktree_free(*out);
		*out = NULL;
		error = worktree_error("path does not match recorded worktree path; moved worktrees are not supported");
	}
done:
	free(requested);
	free(recorded);
	git_repository_free(target);
	return error;
}

static int worktree_submodule(git_submodule *sm, const char *name, void *payload)
{
	(void)sm;
	(void)name;
	*(int *)payload = 1;
	return 0;
}

static int worktree_remove(git_worktree *wt, int force)
{
	git_repository *repo = NULL;
	git_status_list *status = NULL;
	git_status_options status_opts = GIT_STATUS_OPTIONS_INIT;
	git_worktree_prune_options opts = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
	int error, locked, submodules = 0;

	if ((locked = git_worktree_is_locked(NULL, wt)) < 0)
		return locked;
	if (locked)
		return worktree_error("worktree is locked; unlock it before removal");
	if ((error = git_repository_open_from_worktree(&repo, wt)) < 0 ||
		(error = git_submodule_foreach(repo, worktree_submodule, &submodules)) < 0)
		goto done;
	/* Refuse submodules even with force: nested repositories need separate care. */
	if (submodules) {
		error = worktree_error("removing worktrees with submodules is not supported");
		goto done;
	}
	if (!force) {
		status_opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
			GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS | GIT_STATUS_OPT_INCLUDE_IGNORED |
			GIT_STATUS_OPT_RECURSE_IGNORED_DIRS;
		if ((error = git_status_list_new(&status, repo, &status_opts)) < 0)
			goto done;
		if (git_status_list_entrycount(status)) {
			error = worktree_error("worktree contains modified or untracked files; use --force");
			goto done;
		}
	}
	git_status_list_free(status);
	status = NULL;
	git_repository_free(repo);
	repo = NULL;
	opts.flags = GIT_WORKTREE_PRUNE_VALID | GIT_WORKTREE_PRUNE_WORKING_TREE;
	error = git_worktree_prune(wt, &opts);
done:
	git_status_list_free(status);
	git_repository_free(repo);
	return error;
}

int lg2_worktree(git_repository *repo, int argc, char **argv)
{
	git_worktree *wt = NULL;
	int pos = 2, force = 0, end_options = 0, error;

	if (argc < 2)
		return worktree_usage();
	if (!strcmp(argv[1], "add"))
		return worktree_add(repo, argc, argv);
	if (!strcmp(argv[1], "list"))
		return argc == 2 ? worktree_list(repo) : worktree_usage();
	if (strcmp(argv[1], "remove") && strcmp(argv[1], "lock") && strcmp(argv[1], "unlock"))
		return worktree_usage();
	if (!strcmp(argv[1], "remove") && pos < argc && !strcmp(argv[pos], "--force")) {
		force = 1;
		pos++;
	}
	if (pos < argc && !strcmp(argv[pos], "--")) {
		end_options = 1;
		pos++;
	}
	if (argc != pos + 1 || (!end_options && argv[pos][0] == '-'))
		return worktree_usage();
	if ((error = worktree_find(&wt, repo, argv[pos])) != 0)
		return error;
	if (!strcmp(argv[1], "remove"))
		error = worktree_remove(wt, force);
	else if (!strcmp(argv[1], "lock")) {
		error = git_worktree_lock(wt, NULL);
		if (error == GIT_ELOCKED)
			error = worktree_error("worktree is already locked");
	} else {
		error = git_worktree_unlock(wt);
		if (error == 1)
			error = 0;
	}
	git_worktree_free(wt);
	return error;
}
