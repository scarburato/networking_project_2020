//
// Created by dario on 01/01/21.
//

#include <stdlib.h>
#include "peers_info.h"

struct peer_node *peer_list_add(struct peer_node **const root, const peer_definitive_id id, const peer_session_id session_id)
{
	struct peer_node *new_node = calloc(1, sizeof(struct peer_node));
	struct peer_node *i = *root;

	if(!new_node)
		abort();

	new_node->address_id = session_id;
	new_node->value.id = id;

	// Se la lista è vuota mi metto in testa
	if(!*root)
	{
		*root = new_node;
		return new_node;
	}

	// Devo sostiture la testa
	if((*root)->address_id >= new_node->address_id)
	{
		new_node->greater_eq = *root;
		new_node->greater_eq->lesser = new_node;
		*root = new_node;
		return new_node;
	}

	// Aggiunta "nel mezzo"
	for(; i->greater_eq && i->greater_eq->address_id < new_node->address_id; i = i-> greater_eq)
		;

	new_node->greater_eq = i->greater_eq;
	if(i->greater_eq)
		new_node->greater_eq->lesser = new_node;
	i->greater_eq = new_node;
	new_node->lesser = i;

	return new_node;
}

struct peer_node* peer_list_find(struct peer_node *const root, const peer_session_id session_id)
{
	for(struct peer_node *i = root; i; i = i->greater_eq)
		if(session_id == i->address_id)
			return i;
	return 0;
}

void peer_list_remove(struct peer_node **root, struct peer_node *target)
{
	if(!target || !*root)
		return;

	if(*root == target)
		*root = target->greater_eq;

	if(target->greater_eq)
		target->greater_eq->lesser = target->lesser;

	if(target->lesser)
		target->lesser->greater_eq = target->greater_eq;

	free(target);
}

void peer_list_delete(struct peer_node *root)
{
	while(root)
	{
		struct peer_node *del = root;
		root = root->greater_eq;
		free(del);
	}
}

struct peer_node* peer_list_get_tail(struct peer_node *i)
{
	if(!i)
		return i;

	for(; i->greater_eq; i = i->greater_eq)
		;
	return i;
}

void peer_list_get_neighbors(struct peer_node *root, struct peer_node *t, struct peer_node **neighors)
{
	if(!t || !root)
		return;

	// Il vicino posteriore
	neighors[0] = t->lesser ? t->lesser : peer_list_get_tail(root);

	// Il vicino anteriore.
	neighors[1] = t->greater_eq ? t->greater_eq : root;
}

size_t peer_list_size(struct peer_node const *const root)
{
	size_t size = 0;
	for(struct peer_node const *n = root; n; n = n->greater_eq)
		size++;

	return size;
}