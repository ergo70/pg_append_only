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

static char *append_only_relation = NULL;
static char *append_only_relation_schema = NULL;
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
    DefineCustomStringVariable("append_only_filter.append_only_relation",
                               "Sets the Relation which is protected "
                               "against UPDATE and DELETE.",
                               NULL,
                               &append_only_relation,
                               NULL,
                               PGC_SUSET,
                               0, /* no flags required */
                               NULL,
                               NULL,
                               NULL);

    /* Define custom GUC variable. */
    DefineCustomStringVariable("append_only_filter.append_only_relation_schema",
                               "Sets the Schema of the Relation which is protected "
                               "against UPDATE and DELETE.",
                               NULL,
                               &append_only_relation_schema,
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
    char *target_table = NULL;
    char *target_schema = NULL;

    /* this way we can daisy chain planner hooks if necessary */
    if (prev_planner_hook != NULL)
        result = (*prev_planner_hook) (parse, cursorOptions, boundParams);
    else
        result = standard_planner(parse, cursorOptions, boundParams);

    if(append_only_relation != NULL && append_only_relation_schema != NULL && parse->commandType != CMD_SELECT && parse->commandType != CMD_INSERT && parse->commandType != CMD_UTILITY)
    {
        rte = (RangeTblEntry *) rt_fetch(parse->resultRelation, parse->rtable);
        target_table = get_rel_name(rte->relid);
        target_schema = get_namespace_name(get_rel_namespace(rte->relid));

        if(target_table != NULL && target_schema != NULL && strcmp(append_only_relation, target_table) == 0 && strcmp(append_only_relation_schema, target_schema) == 0)
        {
            ereport(ERROR, (errcode(ERRCODE_QUERY_CANCELED), errmsg("Relation %s.%s is append only!", target_schema, target_table)));
        }
    }

    return result;
}
