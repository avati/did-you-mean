/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/*
 *
 * Search for the 'most similar' word in the dictionary
 *
 * Usage: ./a.out /usr/share/dict/words funkyword
 *
 * Bugs: avati@gluster.com
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

#define DISTANCE_EDIT 1
#define DISTANCE_INS  1
#define DISTANCE_DEL  1

struct trienode;
struct trie;

struct trienode {
        char             id;
        char             eow;
        int              depth;
        void            *data;
        struct trienode *parent;
        struct trienode *subnodes[255];
};


struct trie {
        struct trienode   root;
        int               nodecnt;
        const char       *word;
};


struct trie *
trie_new (const char *word)
{
        struct trie  *trie = NULL;

        trie = calloc (sizeof (*trie), 1);
        if (!trie)
                return NULL;

        trie->word = strdup (word);
        if (!trie->word) {
                free (trie);
                trie = NULL;
        }

        trie->root.data = calloc (sizeof (int), strlen (trie->word));
        if (!trie->root.data) {
                free ((void *)trie->word);
                free (trie);
                trie = NULL;
        }

        return trie;
}


struct trienode *
trie_subnode (struct trie *trie, struct trienode *node, int id)
{
        struct trienode *subnode = NULL;

        subnode = node->subnodes[id];
        if (!subnode) {
                subnode = calloc (sizeof (*subnode), 1);
                if (!subnode)
                        return NULL;

                subnode->id        = id;
                subnode->depth     = node->depth + 1;
                subnode->data      = calloc (sizeof (int),
                                             strlen (trie->word));

                node->subnodes[id] = subnode;
                subnode->parent    = node;
                trie->nodecnt++;
        }

        return subnode;
}


int
trie_add (struct trie *trie, const char *word)
{
        struct trienode *node = NULL;
        int              i = 0;
        char             id = 0;
        struct trienode *subnode = NULL;

        node = &trie->root;

        for (i = 0; i < strlen (word); i++) {
                if (isspace (word[i]))
                        break;

                id = word[i];

                subnode = trie_subnode (trie, node, id);
                if (!subnode)
                        return -1;
                node = subnode;
        }

        node->eow = 1;

        return 0;
}


void
trienode_print (struct trienode *node)
{
        struct trienode *trav = NULL;
        int              i = 0;

        printf ("%c", node->id);

        for (i = 0; i < 255; i++) {
                trav = node->subnodes[i];
                if (trav) {
                        printf ("(");
                        trienode_print (trav);
                        printf (")");
                }
        }
}


void
trie_print (struct trie *trie)
{
        struct trienode *node = NULL;

        node = &trie->root;

        trienode_print (node);
        printf ("\n");
}


int
trienode_walk (struct trienode *node, int (*fn)(struct trienode *node,
                                                void *data),
               void *data, int eowonly)
{
        struct trienode *trav = NULL;
        int              i = 0;
        int              ret = 0;

        if (!eowonly || node->eow)
                ret = fn (node, data);

        if (ret)
                goto out;

        for (i = 0; i < 255; i++) {
                trav = node->subnodes[i];

                if (trav)
                        ret += trienode_walk (trav, fn, data, eowonly);

                if (ret < 0)
                        goto out;
        }

out:
        return ret;
}


int
trie_walk (struct trie *trie, int (*fn)(struct trienode *node, void *data),
           void *data, int eowonly)
{
        return trienode_walk (&trie->root, fn, data, eowonly);
}


int
load_dict (struct trie *trie, const char *filename)
{
        FILE *fp = NULL;
        char  word[128] = {0};
        int   cnt = 0;
        int   ret = -1;

        fp = fopen (filename, "r");
        if (!fp)
                return ret;

        while (fgets (word, 128, fp)) {
                ret = trie_add (trie, word);
                if (ret)
                        break;
                cnt++;
        }

        fclose (fp);

        return cnt;
}


int
print_node (struct trienode *node)
{
        if (node->parent) {
                print_node (node->parent);
                printf ("%c", node->id);
        }

        return 0;
}


int
print_if_equal (struct trienode *node, void *data)
{
        int *row = NULL;
        int  ret = 0;
        struct {
                int   len;
                int   wordlen;
        } *msg;

        msg = data;
        row = node->data;

        if (row[msg->wordlen - 1] == msg->len) {
                print_node (node);
                printf (" ");
                ret = 1;
        }

        return ret;
}


int
calc_dist (struct trienode *node, void *data)
{
        const char *word = NULL;
        int         i = 0;
        int        *row = NULL;
        int        *uprow = NULL;
        int         distu = 0;
        int         distl = 0;
        int         distul = 0;

        word = data;

        row = node->data;

        if (!node->parent) {
                for (i = 0; i < strlen (word); i++)
                        row[i] = i+1;

                return 0;
        }

        uprow = node->parent->data;

        distu = node->depth;          /* up node */
        distul = node->parent->depth; /* up-left node */

        for (i = 0; i < strlen (word); i++) {
                distl = uprow[i];     /* left node */

                if (word[i] == node->id)
                        row[i] = distul;
                else
                        row[i] = min ((distul + DISTANCE_EDIT),
                                      min ((distu + DISTANCE_DEL),
                                           (distl + DISTANCE_INS)));

                distu  = row[i];
                distul = distl;
        }

        return 0;
}


int
trie_measure (struct trie *trie, const char *word)
{
        int len = 0;
        int ret = 0;
        int i = 0;

        len = strlen (word);

        fprintf (stderr, "Calculating distances ... ");
        {
                ret = trie_walk (trie, calc_dist, (void *)word, 0);
        }
        fprintf (stderr, "done.\n");

        fprintf (stderr, "Did you mean: ");
        {
                struct {
                        int len;
                        int wordlen;
                } msg;

                msg.wordlen = len;
                for (i = 0; i < len; i++) {
                        msg.len = i;
                        ret = trie_walk (trie, print_if_equal, &msg, 1);
                        if (ret)
                                break;
                }
                fflush (stdout);
        }
        fprintf (stderr, "\n");

        return i;
}


int
main (int argc, char *argv[])
{
        struct trie *trie = NULL;
        int          ret = 0;
        char        *word = NULL;

        if (argc != 3) {
                fprintf (stderr, "Usage: %s <dictfile> <word>\n",
                        argv[0]);
                return 1;
        }

        word = argv[2];

        trie = trie_new (word);

        ret = load_dict (trie, argv[1]);

        if (ret <= 0)
                return 1;

        fprintf (stderr, "Loaded %d words (%d nodes)\n",
                 ret, trie->nodecnt);


        trie_measure (trie, word);
//        trie_print (trie);

        return 0;
}
