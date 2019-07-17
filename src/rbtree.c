#include "rbtree.h"

#include <stdio.h>
#include <stdlib.h>

#define red   1
#define black 2
#define is_red_node(node)   ((node)->color == red)
#define is_black_node(node) ((node)->color == black)
#define color_red(node)     ((node)->color = red)
#define color_black(node)   ((node)->color = black)

static void _insert(struct rbnode *, struct rbnode *, struct rbnode *);
static void _left_rotate(struct rbtree *n, struct rbnode *);
static void _right_rotate(struct rbtree *n, struct rbnode *x);
static void _insert_fixup(struct rbtree *t, struct rbnode *z);
static void _delete_fixup(struct rbtree *t, struct rbnode *x);
static void _inorder_walk(struct rbnode *node, struct rbnode *sentinel);
static struct rbnode* _minimum(struct rbnode *node, struct rbnode *sentinel);

struct rbnode *
rbnode_new(int key, struct tcp_connection *value)
{
	struct rbnode *node = (struct rbnode *)calloc(1, sizeof(*node));
	color_black(node);
	node->key = key;
	node->value = value;
	node->parent = NULL;
	node->left = NULL;
	node->right = NULL;
	return node;
}

struct rbtree*
rbtree_new()
{
	struct rbtree *t = (struct rbtree*)calloc(1, sizeof(*t));
	t->sentinel = rbnode_new(-1, NULL);
	t->root = t->sentinel;
	return t;
}

void
rbtree_free(struct rbtree *t)
{
	free(t->sentinel);
	free(t);
}

void
rbtree_insert(struct rbtree *t, struct rbnode *node)
{
	struct rbnode **root = &t->root;
	if (*root == t->sentinel) {
		node->parent = NULL;
		node->left = t->sentinel;
		node->right = t->sentinel;
		color_black(node);
		*root = node;
		return;
	}

	_insert(*root, node, t->sentinel);
	_insert_fixup(t, node);
}

void
rbtree_delete(struct rbtree* t, struct rbnode* node)
{
	int color;
	struct rbnode** root;
	struct rbnode* subst;
	struct rbnode* temp;

	root = (struct rbnode**)&t->root;

	if (node->left == t->sentinel) {
		temp = node->right;
		subst = node;
	} else if (node->right == t->sentinel) {
		temp = node->left;
		subst = node;
	} else {
		subst = _minimum(node->right, t->sentinel);
		if (subst->left != t->sentinel) {
			temp = subst->left;
		} else {
			temp = subst->right;
		}
	}

	if (subst == *root) {
		*root = temp;
		color_black(temp);

		return;
	}

	color = subst->color;

	if (subst == subst->parent->left) {
		subst->parent->left = temp;
	} else {
		subst->parent->right = temp;
	}

	if (subst == node) {
		temp->parent = subst->parent;
	} else {
		if (subst->parent == node) {
			temp->parent = subst;
		} else {
			temp->parent = subst->parent;
		}

		subst->left = node->left;
		subst->right = node->right;
		subst->parent = node->parent;
		subst->color = node->color;

		if (node == *root) {
			*root = subst;
		} else {
			if (node == node->parent->left) {
				node->parent->left = subst;
			} else {
				node->parent->right = subst;
			}
		}

		if (subst->left != t->sentinel) {
			subst->left->parent = subst;
		}

		if (subst->right != t->sentinel) {
			subst->right->parent = subst;
		}
	}

	if (color == black) {
		_delete_fixup(t, temp);
	}
}

struct rbnode*
rbtree_search(struct rbtree* t, int key)
{
	struct rbnode* node = t->root;
	while (node != t->sentinel && node->key != key) {
		if (key < node->key) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	if (node == t->sentinel) {
		return NULL;
	}

	return node;
}

void
rbtree_inorder_walk(struct rbtree* t)
{
	_inorder_walk(t->root, t->sentinel);
}

/*
 * Left Rotaion on node x:
 *     x                 y
 *   /   \             /   \
 *  𝛂     y    ->     x     𝛄
 *      /   \       /   \
 *     𝛃     𝛄     𝛂     𝛃   
 */
static inline void
_left_rotate(struct rbtree* t, struct rbnode* x)
{
	struct rbnode* y = x->right;
	x->right = y->left;

	if (y->left != t->sentinel) {
		y->left->parent = x;
	}

	y->parent = x->parent;

	if (x == t->root) {
		t->root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}

	y->left = x;
	x->parent = y;
}

/*
 * Right Rotation on node x:
 *        x            y
 *      /   \        /   \
 *     y     𝛄  ->  𝛂     x
 *   /   \              /   \
 *  𝛂     𝛃            𝛃     𝛄
 */
static inline void
_right_rotate(struct rbtree* t, struct rbnode* x)
{
	struct rbnode* y = x->left;
	x->left = y->right;

	if (y->right != t->sentinel) {
		y->right->parent = x;
	}

	y->parent = x->parent;

	if (x == t->root) {
		t->root = y;
	} else if (x == x->parent->right) {
		x->parent->right = y; 
	} else {
		x->parent->left = y;
	}

	y->right = x;
	x->parent = y;
}

/*
 * Case 1: z's uncle is red;
 * Case 2: z's uncle is black, and z is a right child
 * Case 3: z's uncle is black, and z is a left child
 */
static inline void
_insert_fixup(struct rbtree* t, struct rbnode* z)
{
	struct rbnode* y;

	while (z != t->root && is_red_node(z->parent)) {
		if (z->parent == z->parent->parent->left) {
			y = z->parent->parent->right;

			if (is_red_node(y)) {
				color_black(z->parent);        /* case 1 */
				color_black(y);                /* case 1 */
				color_red(z->parent->parent);  /* case 1 */
				z = z->parent->parent;         /* case 1 */
			} else {
				if (z == z->parent->right) {
					z = z->parent;        /* case 2 */
					_left_rotate(t, z);   /* case 2 */
				}

				color_black(z->parent);        /* case 3 */
				color_red(z->parent->parent);  /* case 3 */
				_right_rotate(t, z->parent->parent);  /* case 3 */
			}
		} else {
			y = z->parent->parent->left;

			if (is_red_node(y)) {
				color_black(z->parent);
				color_black(y);
				color_red(z->parent->parent);
				z = z->parent->parent;
			} else {
				if (z == z->parent->left) {
					z = z->parent;
					_right_rotate(t, z);
				}
				color_black(z->parent);
				color_red(z->parent->parent);
				_left_rotate(t, z->parent->parent);
			}
		}
	}

	color_black(t->root);
}

static inline void
_insert(struct rbnode* temp, struct rbnode* node, struct rbnode* sentinel)
{
	struct rbnode** p;
	while (1) {
		p = (node->key < temp->key) ? &temp->left : &temp->right;
		if (*p == sentinel) {
			break;
		}
		temp = *p;
	}

	*p = node;
	node->parent = temp;
	node->left = sentinel;
	node->right = sentinel;
	color_red(node);
}

static inline void
_delete_fixup(struct rbtree* t, struct rbnode* x)
{
	struct rbnode* w;
	while (x != t->root && is_black_node(x)) {
		if (x == x->parent->left) {
			w = x->parent->right;

			if (is_red_node(w)) {
				color_black(w);              /* case 1 */
				color_red(x->parent)  ;      /* case 1 */
				_left_rotate(t, x->parent);  /* case 1 */
				w = x->parent->right;        /* case 1 */
			}

			if (is_black_node(w->left) && is_black_node(w->right)) {
				color_red(w);                /* case 2 */
				x = x->parent;               /* case 2 */
			} else { 
				if (is_black_node(w->right)) {
					color_black(w->left);  /* case 3 */
					color_red(w);          /* case 3 */
					_right_rotate(t, w);   /* case 3 */
					w = x->parent->right;  /* case 3 */
				}

				w->color = x->parent->color;   /* case 4 */
				color_black(x->parent);        /* case 4 */
				color_black(w->right);         /* case 4 */
				_left_rotate(t, x->parent);    /* case 4 */
				x = t->root;                   /* case 4 */
			}
		} else {
			w = x->parent->left;

			if (is_red_node(w)) {
				color_black(w);               /* case 1 */
				color_red(x->parent);         /* case 1 */
				_right_rotate(t, x->parent);  /* case 1 */
				w = x->parent->left;          /* case 1 */
			}

			if (is_black_node(w->right) && is_black_node(w->left)) {
				color_red(w);                 /* case 2 */
				x = x->parent;                /* case 2 */
			} else { 
				if (is_black_node(w->left)) {
					color_black(w->right);  /* case 3 */
					color_red(w);           /* case 3 */
					_left_rotate(t, w);     /* case 3 */
					w = x->parent->left;    /* case 3 */
				}

				w->color = x->parent->color;    /* case 4 */
				color_black(x->parent);         /* case 4 */
				color_black(w->left);           /* case 4 */
				_right_rotate(t, x->parent);    /* case 4 */
				x = t->root;                    /* case 4 */
			}
		}
	}

	color_black(x);
}

static inline struct rbnode*
_minimum(struct rbnode* node, struct rbnode* sentinel)
{
	while (node->left != sentinel) {
		node = node->left;
	}
	return node;
}

static inline void
_inorder_walk(struct rbnode* node, struct rbnode* sentinel)
{
	if (node != sentinel) {
		_inorder_walk(node->left, sentinel);
		_inorder_walk(node->right, sentinel);
	}
}

