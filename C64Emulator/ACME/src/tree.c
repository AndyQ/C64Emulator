// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2016 Marco Baye
// Have a look at "acme.c" for further info
//
// tree stuff
#include "tree.h"
#include "config.h"
#include "dynabuf.h"
#include "global.h"
#include "platform.h"


// Functions

// Compute hash value by exclusive ORing the node's ID string and write
// output to struct.
// This function is not allowed to change GlobalDynaBuf!
hash_t make_hash(struct ronode *node) {
	register char		byte;
	register const char	*read;
	register hash_t		tmp	= 0;

	read = node->id_string;
	while ((byte = *read++))
		tmp = ((tmp << 7) | (tmp >> (8 * sizeof(hash_t) - 7))) ^ byte;
	node->hash_value = tmp;
	return tmp;
}

// Link a predefined data set to a tree
void add_node_to_tree(struct ronode **tree, struct ronode *node_to_add)
{
	hash_t	hash;

	// compute hash value
	hash = make_hash(node_to_add);
	while (*tree) {
		// compare HashValue
		if (hash > (*tree)->hash_value)
			tree = &((*tree)->greater_than);
		else
			tree = &((*tree)->less_than_or_equal);
	}
	*tree = node_to_add;	// add new leaf to tree
// New nodes are always added as leaves, so there's no need to copy a second
// pointer. And because the PREDEF* macros contain NULL as init values, it is
// not necessary to clear the new node's greater_than and less_than_or_equal
// fields.
}

// Add predefined tree items to given tree. The PREDEF* macros set HashValue
// to 1 in all entries but the last. The last entry contains 0.
void Tree_add_table(struct ronode **tree, struct ronode *table_to_add)
{
	// Caution when trying to optimise this. :)
	while (table_to_add->hash_value)
		add_node_to_tree(tree, table_to_add++);
	add_node_to_tree(tree, table_to_add);
}

void Tree_reset_table( struct ronode *table_to_reset)
{
    while (!table_to_reset->is_last) {
        table_to_reset->greater_than = NULL;
        table_to_reset->less_than_or_equal = NULL;
        table_to_reset->hash_value = 1;
        table_to_reset ++;
    }
    table_to_reset->hash_value = 0;
}


// Search for a given ID string in a given tree.
// Compute the hash of the given string and then use that to try to find a
// tree item that matches the given data (HashValue and DynaBuf-String).
// Store "body" component in node_body and return TRUE.
// Return FALSE if no matching item found.
int Tree_easy_scan(struct ronode *tree, void **node_body, struct dynabuf *dyna_buf)
{
	struct ronode	wanted;	// temporary storage
	const char	*p1,
			*p2;
	char		b1,
			b2;
	hash_t		hash;

	wanted.id_string = dyna_buf->buffer;
	hash = make_hash(&wanted);
	while (tree) {
		// compare HashValue
		if (hash > tree->hash_value) {
			// wanted hash is bigger than current, so go
			// to tree branch with bigger hashes
			tree = tree->greater_than;
			continue;
		}
		if (hash == tree->hash_value) {
			p1 = wanted.id_string;
			p2 = tree->id_string;
			do {
				b1 = *p1++;
				b2 = *p2++;
			} while ((b1 == b2) && b1);
			if (b1 == b2) {
				// store body data
				*node_body = tree->body;
				return TRUE;
			}
		}
		// either the wanted hash is smaller or
		// it was exact but didn't match
		tree = tree->less_than_or_equal;
	}
	return FALSE ;	// indicate failure
}

// Search for a "RAM tree" item. Compute the hash of string in GlobalDynaBuf
// and then use that to try to find a tree item that matches the given data
// (HashValue, ID_Number, GlobalDynaBuf-String). Save pointer to found tree
// item in given location.
// If no matching item is found, check the "create" flag. If it is set, create
// a new tree item, link to tree, fill with data and store its pointer. If the
// "create" flag is zero, store NULL as result.
// Returns whether item was created.
int Tree_hard_scan(struct rwnode **result, struct rwnode **forest, int id_number, int create)
{
	struct ronode	wanted;	// temporary storage
	struct rwnode	**current_node;
	struct rwnode	*new_leaf_node;
	const char	*p1,
			*p2;
	char		b1,
			b2;
	hash_t		byte_hash;

	wanted.id_string = GLOBALDYNABUF_CURRENT;
	// incorporate ID number into hash value
	byte_hash = make_hash(&wanted) ^ id_number;
	wanted.hash_value = byte_hash;	// correct struct's hash
	PLATFORM_UINT2CHAR(byte_hash);	// transform into byte

	current_node = &(forest[byte_hash]);	// point into table
	while (*current_node) {
		// compare HashValue
		if (wanted.hash_value > (*current_node)->hash_value) {
			// wanted hash is bigger than current, so go
			// to tree branch with bigger hashes
			current_node = &((*current_node)->greater_than);
			continue;
		}
		if (wanted.hash_value == (*current_node)->hash_value) {
			if (id_number == (*current_node)->id_number) {
				p1 = wanted.id_string;
				p2 = (*current_node)->id_string;
				do {
					b1 = *p1++;
					b2 = *p2++;
				} while ((b1 == b2) && b1);
				if (b1 == b2) {
					// store node pointer
					*result = *current_node;
					// return FALSE because node
					// was not created
					return FALSE;
				}
			}
		}
		// either the wanted hash is smaller or
		// it was exact but didn't match
		current_node = &((*current_node)->less_than_or_equal);
	}
	// node wasn't found. Check whether to create it
	if (create == FALSE) {
		*result = NULL;	// indicate failure
		return FALSE;	// return FALSE because node was not created
	}
	// create new node
	new_leaf_node = safe_malloc(sizeof(*new_leaf_node));
	new_leaf_node->greater_than = NULL;
	new_leaf_node->less_than_or_equal = NULL;
	new_leaf_node->hash_value = wanted.hash_value;
	new_leaf_node->id_number = id_number;
	new_leaf_node->id_string = DynaBuf_get_copy(GlobalDynaBuf);	// make permanent copy
	// add new leaf to tree
	*current_node = new_leaf_node;
	// store pointer to new node in result location
	*result = new_leaf_node;
	return TRUE;	// return TRUE because node was created
}

// Call given function for each object of matching type in the given tree.
// Calls itself recursively.
void dump_tree(struct rwnode *node, int id_number, void (*fn)(struct rwnode *, FILE *), FILE *env)
{

	if (node->id_number == id_number)
		fn(node, env);
	if (node->greater_than)
		dump_tree(node->greater_than, id_number, fn, env);
	if (node->less_than_or_equal)
		dump_tree(node->less_than_or_equal, id_number, fn, env);
}

// Calls Tree_dump_tree for each non-zero entry of the given tree table.
void Tree_dump_forest(struct rwnode **forest, int id_number, void (*fn)(struct rwnode *, FILE *), FILE *env)
{
	int	ii;

	for (ii = 255; ii >= 0; --ii) {
		if (*forest)
			dump_tree(*forest, id_number, fn, env);
		++forest;
	}
}
