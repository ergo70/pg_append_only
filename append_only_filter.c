/*-------------------------------------------------------------------------
 *
 * append_only_filter.c
 *
 * Loadable PostgreSQL module to filter statements according to configured
 * criteria and stop them before they start to run.
 *
 * The currently implemented criterion is the plan's target relation for
 * UPDATE / DELETE.
 *
 * Copyright 2018 Ernst-Georg Schmid
 *
 * Distributed under The PostgreSQL License
 * see License file for terms
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "optimizer/planner.h"
#include "parser/parsetree.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"

PG_MODULE_MAGIC;

static char *append_only_relations = NULL;
static bool module_loaded = false;
static planner_hook_type prev_planner_hook = NULL;

static PlannedStmt *protect_function(Query *parse,
                                     int cursorOptions,
                                     ParamListInfo boundParams);

void		_PG_init(void);
void		_PG_fini(void);

/*
 * Module load callback
 */
void
_PG_init(void)
{
    /* Define custom GUC variable. */
    DefineCustomStringVariable("append_only_filter.append_only_relations",
                               "Sets the relation(s) which is protected "
                               "against UPDATE and DELETE.",
                               NULL,
                               &append_only_relations,
                               NULL,
                               PGC_SUSET,
                               0, /* no flags required */
                               NULL,
                               NULL,
                               NULL);

    /* Define custom GUC variable. */
    DefineCustomBoolVariable("append_only_filter.module_loaded",
                             "true if the module is loaded ",
                             NULL,
                             &module_loaded,
                             true,
                             PGC_BACKEND,
                             0, /* no flags required */
                             NULL,
                             NULL,
                             NULL);

    /* install the hook */
    prev_planner_hook = planner_hook;
    planner_hook = protect_function;

}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
    /* Uninstall hook. */
    planner_hook = prev_planner_hook;
    /* reset loaded var */
    module_loaded = false;
}

/*
 * Limit function
 */
static PlannedStmt *
protect_function(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
    PlannedStmt *result = NULL;
    RangeTblEntry *rte = NULL;
    int bufsz = 2*NAMEDATALEN;
    char *target_table = NULL;
    char *target_schema = NULL;
    char *rest = NULL;
    char *token = NULL;
    char *append_only_relations_copy;
    char target_relation[bufsz];
    char append_only_relation[bufsz];
    char format[6] = {0x0,0x0,0x0,0x0,0x0,0x0};

    /* this way we can daisy chain planner hooks if necessary */
    if (prev_planner_hook != NULL)
        result = (*prev_planner_hook) (parse, cursorOptions, boundParams);
    else
        result = standard_planner(parse, cursorOptions, boundParams);

    if(bufsz < 1000 && append_only_relations != NULL && parse->commandType != CMD_SELECT && parse->commandType != CMD_INSERT && parse->commandType != CMD_UTILITY)
    {
        rte = (RangeTblEntry *) rt_fetch(parse->resultRelation, parse->rtable);
        target_table = get_rel_name(rte->relid);
        target_schema = get_namespace_name(get_rel_namespace(rte->relid));

        if(target_table != NULL && target_schema != NULL)
        {
            memset(&target_relation[0], 0x0, bufsz);
            snprintf(target_relation, bufsz - 1, "%s.%s", target_schema, target_table);
            snprintf(format, 6, "%%%is", bufsz - 1);

            append_only_relations_copy = palloc0(strlen(append_only_relations + 1));
            memcpy(append_only_relations_copy, append_only_relations, strlen(append_only_relations));

            token = strtok_r(append_only_relations_copy, ",", &rest);

            while(token != NULL)
            {
                // Trim whitespaces
                sscanf(token, format, append_only_relation);

                if(strcmp(append_only_relation, target_relation) == 0)
                {
                    ereport(ERROR, (errcode(ERRCODE_QUERY_CANCELED), errmsg("Relation %s is append only!", target_relation)));
                }

                token = strtok_r(NULL, ",", &rest);
            }
        }
    }

    return result;
}
