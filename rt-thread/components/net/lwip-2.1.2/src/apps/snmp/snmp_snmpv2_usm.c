/*
Generated by LwipMibCompiler
*/

#include "lwip/apps/snmp_opts.h"
#if LWIP_SNMP && LWIP_SNMP_V3

#include "lwip/apps/snmp_snmpv2_usm.h"
#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_core.h"
#include "lwip/apps/snmp_scalar.h"
#include "lwip/apps/snmp_table.h"
#include "lwip/apps/snmpv3.h"
#include "snmpv3_priv.h"

#include "lwip/apps/snmp_snmpv2_framework.h"

#include <string.h>

/* --- usmUser 1.3.6.1.6.3.15.1.2 ----------------------------------------------------- */

static const struct snmp_oid_range usmUserTable_oid_ranges[] =
{
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff },
    { 0, 0xff }, { 0, 0xff }, { 0, 0xff }, { 0, 0xff }
};

static void snmp_engineid_to_oid(const char* engineid, u32_t* oid, u32_t len)
{
    u8_t i;

    for (i = 0; i < len; i++)
    {
        oid[i] = engineid[i];
    }
}

static void snmp_oid_to_name(char* name, const u32_t* oid, size_t len)
{
    u8_t i;

    for (i = 0; i < len; i++)
    {
        name[i] = (char)oid[i];
    }
}

static void snmp_name_to_oid(const char* name, u32_t* oid, size_t len)
{
    u8_t i;

    for (i = 0; i < len; i++)
    {
        oid[i] = name[i];
    }
}

static const struct snmp_obj_id* snmp_auth_algo_to_oid(snmpv3_auth_algo_t algo)
{
    if (algo == SNMP_V3_AUTH_ALGO_MD5)
    {
        return &usmHMACMD5AuthProtocol;
    }
    else if (algo ==  SNMP_V3_AUTH_ALGO_SHA)
    {
        return &usmHMACMD5AuthProtocol;
    }

    return &usmNoAuthProtocol;
}

static const struct snmp_obj_id* snmp_priv_algo_to_oid(snmpv3_priv_algo_t algo)
{
    if (algo == SNMP_V3_PRIV_ALGO_DES)
    {
        return &usmDESPrivProtocol;
    }
    else if (algo == SNMP_V3_PRIV_ALGO_AES)
    {
        return &usmAESPrivProtocol;
    }

    return &usmNoPrivProtocol;
}

char username[32];

static snmp_err_t usmusertable_get_instance(const u32_t* column, const u32_t* row_oid, u8_t row_oid_len, struct snmp_node_instance* cell_instance)
{
    const char* engineid;
    u8_t eid_len;

    u32_t engineid_oid[SNMP_V3_MAX_ENGINE_ID_LENGTH];

    u8_t name_len;
    u8_t engineid_len;

    u8_t name_start;
    u8_t engineid_start;

    LWIP_UNUSED_ARG(column);

    snmpv3_get_engine_id(&engineid, &eid_len);

    engineid_len = (u8_t)row_oid[0];
    engineid_start = 1;

    if (engineid_len != eid_len)
    {
        /* EngineID length does not match! */
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    if (engineid_len > row_oid_len)
    {
        /* row OID doesn't contain enough data according to engineid_len.*/
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    /* check if incoming OID length and if values are in plausible range */
    if (!snmp_oid_in_range(&row_oid[engineid_start], engineid_len, usmUserTable_oid_ranges, engineid_len))
    {
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    snmp_engineid_to_oid(engineid, engineid_oid, engineid_len);

    /* Verify EngineID */
    if (snmp_oid_equal(&row_oid[engineid_start], engineid_len, engineid_oid, engineid_len))
    {
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    name_len = (u8_t)row_oid[engineid_start + engineid_len];
    name_start = engineid_start + engineid_len + 1;

    if (name_len > SNMP_V3_MAX_USER_LENGTH)
    {
        /* specified name is too long */
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    if (1 + engineid_len + 1 + name_len != row_oid_len)
    {
        /* Length of EngineID and name does not match row oid length. (+2 for length fields)*/
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    /* check if incoming OID length and if values are in plausible range */
    if (!snmp_oid_in_range(&row_oid[name_start], name_len, usmUserTable_oid_ranges, name_len))
    {
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    /* Verify if user exists */
    memset(username, 0, sizeof(username));
    snmp_oid_to_name(username, &row_oid[name_start], name_len);
    if (snmpv3_get_user(username, NULL, NULL, NULL, NULL) != ERR_OK)
    {
        return SNMP_ERR_NOSUCHINSTANCE;
    }

    /* Save name in reference pointer to make it easier to handle later on */
    cell_instance->reference.ptr = username;
    cell_instance->reference_len = name_len;

    /* user was found */
    return SNMP_ERR_NOERROR;
}

/*
 * valid oid options
 * <oid>
 * <oid>.<EngineID length>
 * <oid>.<EngineID length>.<partial EngineID>
 * <oid>.<EngineID length>.<EngineID>
 * <oid>.<EngineID length>.<EngineID>.<UserName length>
 * <oid>.<EngineID length>.<EngineID>.<UserName length>.<partial UserName>
 * <oid>.<EngineID length>.<EngineID>.<UserName length>.<UserName>
 *
 */
static snmp_err_t usmusertable_get_next_instance(const u32_t* column, struct snmp_obj_id* row_oid, struct snmp_node_instance* cell_instance)
{
    const char* engineid;
    u8_t eid_len;

    u32_t engineid_oid[SNMP_V3_MAX_ENGINE_ID_LENGTH];

    u8_t name_len;
    u8_t engineid_len;

    u8_t name_start;
    u8_t engineid_start = 1;
    u8_t i;

    struct snmp_next_oid_state state;

    u32_t result_temp[LWIP_ARRAYSIZE(usmUserTable_oid_ranges)];

    LWIP_UNUSED_ARG(column);

    snmpv3_get_engine_id(&engineid, &eid_len);

    /* If EngineID might be given */
    if (row_oid->len > 0)
    {
        engineid_len = (u8_t)row_oid->id[0];
        engineid_start = 1;

        if (engineid_len != eid_len)
        {
            /* EngineID length does not match! */
            return SNMP_ERR_NOSUCHINSTANCE;
        }

        if (engineid_len > row_oid->len)
        {
            /* Verify partial EngineID */
            snmp_engineid_to_oid(engineid, engineid_oid, row_oid->len - 1);
            if (!snmp_oid_equal(&row_oid->id[engineid_start], row_oid->len - 1, engineid_oid, row_oid->len - 1))
            {
                return SNMP_ERR_NOSUCHINSTANCE;
            }
        }
        else
        {
            /* Verify complete EngineID */
            snmp_engineid_to_oid(engineid, engineid_oid, engineid_len);
            if (!snmp_oid_equal(&row_oid->id[engineid_start], engineid_len, engineid_oid, engineid_len))
            {
                return SNMP_ERR_NOSUCHINSTANCE;
            }
        }

        /* At this point, the given EngineID (partially) matches the local EngineID.*/

        /* If name might also be given */
        if (row_oid->len > engineid_start + engineid_len)
        {
            name_len = (u8_t)row_oid->id[engineid_start + engineid_len];
            name_start = engineid_start + engineid_len + 1;

            if (name_len > SNMP_V3_MAX_USER_LENGTH)
            {
                /* specified name is too long, max length is 32 according to mib file.*/
                return SNMP_ERR_NOSUCHINSTANCE;
            }

            if (row_oid->len < engineid_len + name_len + 2)
            {
                /* Partial name given according to oid.*/
                u8_t tmplen = row_oid->len - engineid_len - 2;
                if (!snmp_oid_in_range(&row_oid->id[name_start], tmplen, usmUserTable_oid_ranges, tmplen))
                {
                    return SNMP_ERR_NOSUCHINSTANCE;
                }
            }
            else
            {
                /* Full name given according to oid. Also test for too much data.*/
                u8_t tmplen = row_oid->len - engineid_len - 2;
                if (!snmp_oid_in_range(&row_oid->id[name_start], name_len, usmUserTable_oid_ranges, tmplen))
                {
                    return SNMP_ERR_NOSUCHINSTANCE;
                }
            }

            /* At this point the EngineID and (partial) UserName match the local EngineID and UserName.*/
        }
    }

    /* init struct to search next oid */
    snmp_next_oid_init(&state, row_oid->id, row_oid->len, result_temp, LWIP_ARRAYSIZE(usmUserTable_oid_ranges));

    for (i = 0; i < snmpv3_get_amount_of_users(); i++)
    {
        u32_t test_oid[LWIP_ARRAYSIZE(usmUserTable_oid_ranges)];

        test_oid[0] = eid_len;
        snmp_engineid_to_oid(engineid, &test_oid[1], eid_len);

        snmpv3_get_username(username, i);

        test_oid[1 + eid_len] = strlen(username);
        snmp_name_to_oid(username, &test_oid[2 + eid_len], strlen(username));

        /* check generated OID: is it a candidate for the next one? */
        snmp_next_oid_check(&state, test_oid, (u8_t)(1 + eid_len + 1 + strlen(username)), LWIP_PTR_NUMERIC_CAST(void*, i));
    }

    /* did we find a next one? */
    if (state.status == SNMP_NEXT_OID_STATUS_SUCCESS)
    {
        snmp_oid_assign(row_oid, state.next_oid, state.next_oid_len);
        /* store username for subsequent operations (get/test/set) */
        memset(username, 0, sizeof(username));
        snmpv3_get_username(username, LWIP_PTR_NUMERIC_CAST(u8_t, state.reference));
        cell_instance->reference.ptr = username;
        cell_instance->reference_len = strlen(username);
        return SNMP_ERR_NOERROR;
    }

    /* not found */
    return SNMP_ERR_NOSUCHINSTANCE;
}

static s16_t usmusertable_get_value(struct snmp_node_instance* cell_instance, void* value)
{
    snmpv3_user_storagetype_t storage_type;

    switch (SNMP_TABLE_GET_COLUMN_FROM_OID(cell_instance->instance_oid.id))
    {
        case 3: /* usmUserSecurityName */
            MEMCPY(value, cell_instance->reference.ptr, cell_instance->reference_len);
            return (s16_t)cell_instance->reference_len;
        case 4: /* usmUserCloneFrom */
            MEMCPY(value, snmp_zero_dot_zero.id, snmp_zero_dot_zero.len * sizeof(u32_t));
            return snmp_zero_dot_zero.len * sizeof(u32_t);
        case 5:   /* usmUserAuthProtocol */
        {
            const struct snmp_obj_id* auth_algo;
            snmpv3_auth_algo_t auth_algo_val;
            snmpv3_get_user((const char*)cell_instance->reference.ptr, &auth_algo_val, NULL, NULL, NULL);
            auth_algo = snmp_auth_algo_to_oid(auth_algo_val);
            MEMCPY(value, auth_algo->id, auth_algo->len * sizeof(u32_t));
            return auth_algo->len * sizeof(u32_t);
        }
        case 6: /* usmUserAuthKeyChange */
            return 0;
        case 7: /* usmUserOwnAuthKeyChange */
            return 0;
        case 8:   /* usmUserPrivProtocol */
        {
            const struct snmp_obj_id* priv_algo;
            snmpv3_priv_algo_t priv_algo_val;
            snmpv3_get_user((const char*)cell_instance->reference.ptr, NULL, NULL, &priv_algo_val, NULL);
            priv_algo = snmp_priv_algo_to_oid(priv_algo_val);
            MEMCPY(value, priv_algo->id, priv_algo->len * sizeof(u32_t));
            return priv_algo->len * sizeof(u32_t);
        }
        case 9: /* usmUserPrivKeyChange */
            return 0;
        case 10: /* usmUserOwnPrivKeyChange */
            return 0;
        case 11: /* usmUserPublic */
            /* TODO: Implement usmUserPublic */
            return 0;
        case 12: /* usmUserStorageType */
            snmpv3_get_user_storagetype((const char*)cell_instance->reference.ptr, &storage_type);
            *(s32_t*)value = storage_type;
            return sizeof(s32_t);
        case 13: /* usmUserStatus */
            *(s32_t*)value = 1;  /* active */
            return sizeof(s32_t);
        default:
            LWIP_DEBUGF(SNMP_MIB_DEBUG, ("usmusertable_get_value(): unknown id: %"S32_F"\n", SNMP_TABLE_GET_COLUMN_FROM_OID(cell_instance->instance_oid.id)));
            return 0;
    }
}

/* --- usmMIBObjects 1.3.6.1.6.3.15.1 ----------------------------------------------------- */
static s16_t usmstats_scalars_get_value(const struct snmp_scalar_array_node_def* node, void* value)
{
    u32_t* uint_ptr = (u32_t*)value;
    switch (node->oid)
    {
        case 1: /* usmStatsUnsupportedSecLevels */
            *uint_ptr = snmp_stats.unsupportedseclevels;
            break;
        case 2: /* usmStatsNotInTimeWindows */
            *uint_ptr = snmp_stats.notintimewindows;
            break;
        case 3: /* usmStatsUnknownUserNames */
            *uint_ptr = snmp_stats.unknownusernames;
            break;
        case 4: /* usmStatsUnknownEngineIDs */
            *uint_ptr = snmp_stats.unknownengineids;
            break;
        case 5: /* usmStatsWrongDigests */
            *uint_ptr = snmp_stats.wrongdigests;
            break;
        case 6: /* usmStatsDecryptionErrors */
            *uint_ptr = snmp_stats.decryptionerrors;
            break;
        default:
            LWIP_DEBUGF(SNMP_MIB_DEBUG, ("usmstats_scalars_get_value(): unknown id: %"S32_F"\n", node->oid));
            return 0;
    }

    return sizeof(*uint_ptr);
}

/* --- snmpUsmMIB  ----------------------------------------------------- */

/* --- usmUser 1.3.6.1.6.3.15.1.2 ----------------------------------------------------- */

static const struct snmp_table_col_def usmusertable_columns[] =
{
    {3,  SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserSecurityName */
    {4,  SNMP_ASN1_TYPE_OBJECT_ID,    SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserCloneFrom */
    {5,  SNMP_ASN1_TYPE_OBJECT_ID,    SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserAuthProtocol */
    {6,  SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserAuthKeyChange */
    {7,  SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserOwnAuthKeyChange */
    {8,  SNMP_ASN1_TYPE_OBJECT_ID,    SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserPrivProtocol */
    {9,  SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserPrivKeyChange */
    {10, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserOwnPrivKeyChange */
    {11, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserPublic */
    {12, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserStorageType */
    {13, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY}, /* usmUserStatus */
};
static const struct snmp_table_node usmusertable = SNMP_TABLE_CREATE(2, usmusertable_columns, usmusertable_get_instance, usmusertable_get_next_instance, usmusertable_get_value, NULL, NULL);

static const struct snmp_node* const usmuser_subnodes[] =
{
    &usmusertable.node.node
};
static const struct snmp_tree_node usmuser_treenode = SNMP_CREATE_TREE_NODE(2, usmuser_subnodes);

/* --- usmMIBObjects 1.3.6.1.6.3.15.1 ----------------------------------------------------- */
static const struct snmp_scalar_array_node_def usmstats_scalars_nodes[] =
{
    {1, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsUnsupportedSecLevels */
    {2, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsNotInTimeWindows */
    {3, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsUnknownUserNames */
    {4, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsUnknownEngineIDs */
    {5, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsWrongDigests */
    {6, SNMP_ASN1_TYPE_COUNTER, SNMP_NODE_INSTANCE_READ_ONLY}, /* usmStatsDecryptionErrors */
};
static const struct snmp_scalar_array_node usmstats_scalars = SNMP_SCALAR_CREATE_ARRAY_NODE(1, usmstats_scalars_nodes, usmstats_scalars_get_value, NULL, NULL);

static const struct snmp_node* const usmmibobjects_subnodes[] =
{
    &usmstats_scalars.node.node,
    &usmuser_treenode.node
};
static const struct snmp_tree_node usmmibobjects_treenode = SNMP_CREATE_TREE_NODE(1, usmmibobjects_subnodes);

/* --- snmpUsmMIB  ----------------------------------------------------- */
static const struct snmp_node* const snmpusmmib_subnodes[] =
{
    &usmmibobjects_treenode.node
};
static const struct snmp_tree_node snmpusmmib_root = SNMP_CREATE_TREE_NODE(15, snmpusmmib_subnodes);
static const u32_t snmpusmmib_base_oid[] = {1, 3, 6, 1, 6, 3, 15};
const struct snmp_mib snmpusmmib = {snmpusmmib_base_oid, LWIP_ARRAYSIZE(snmpusmmib_base_oid), &snmpusmmib_root.node};

#endif /* LWIP_SNMP */
