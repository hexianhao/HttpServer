#ifndef __RBTREE_H
#define __RBTREE_H

#include <stdint.h>

/* 给节点着色，1表示红色，0表示黑色  */
#define rbt_red(node)               ((node)->color = 1)
#define rbt_black(node)             ((node)->color = 0)
/* 判断节点的颜色 */
#define rbt_is_red(node)            ((node)->color)
#define rbt_is_black(node)          (!rbt_is_red(node))
/* 复制某个节点的颜色 */
#define rbt_copy_color(n1, n2)      (n1->color = n2->color)

/* 节点着黑色的宏定义 */
/* a sentinel must be black */
#define rbtree_sentinel_init(node)  rbt_black(node)

/* 初始化红黑树，即为空的红黑树 */
/* tree 是指向红黑树的指针，
 * s 是红黑树的一个NIL节点，
 * i 表示函数指针，决定节点是新增还是替换
 */
#define rbtree_init(tree, s, i)                                               \
    rbtree_sentinel_init(s);                                                  \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i          


typedef uint64_t rbtree_key_t;
typedef struct rbtree_node_s rbtree_node_t;
typedef struct rbtree_s rbtree_t;

typedef void (*rbtree_insert_pt)(rbtree_node_t *root, rbtree_node_t *node, rbtree_node_t *sentinel);

struct rbtree_node_s {
    rbtree_key_t    key;
    rbtree_node_t  *left;
    rbtree_node_t  *right;
    rbtree_node_t  *parent;
    unsigned char   color;
    unsigned char   data;
};

struct rbtree_s {
    rbtree_node_t     *root;       // 根节点
    rbtree_node_t     *sentinel;   // nil节点
    rbtree_insert_pt   insert;
};

void rbtree_insert(rbtree_t *tree, rbtree_node_t *node);
void rbtree_insert_value(rbtree_node_t *temp, rbtree_node_t *node, rbtree_node_t *sentinel);
void rbtree_delete(rbtree_t *tree, rbtree_node_t *node);
rbtree_node_t *rbtree_next(rbtree_t *tree, rbtree_node_t *node);
void rbtree_insert_timer_value(rbtree_node_t *temp, rbtree_node_t *node, rbtree_node_t *sentinel);

/* 获取红黑树键值最小的节点 */
static inline rbtree_node_t *
rbtree_min(rbtree_node_t *node, rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}

#endif