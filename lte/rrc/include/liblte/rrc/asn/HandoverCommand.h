/*
 * Generated by asn1c-0.9.28 (http://lionet.info/asn1c)
 * From ASN.1 module "EUTRA-InterNodeDefinitions"
 * 	found in "EUTRA-InterNodeDefinitions.asn"
 */

#ifndef	_HandoverCommand_H_
#define	_HandoverCommand_H_


#include <asn_application.h>

/* Including external dependencies */
#include "HandoverCommand-r8-IEs.h"
#include <NULL.h>
#include <constr_CHOICE.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dependencies */
typedef enum criticalExtensions_PR {
	criticalExtensions_PR_NOTHING,	/* No components present */
	criticalExtensions_PR_c1,
	criticalExtensions_PR_criticalExtensionsFuture
} criticalExtensions_PR;
typedef enum c1_PR {
	c1_PR_NOTHING,	/* No components present */
	c1_PR_handoverCommand_r8,
	c1_PR_spare7,
	c1_PR_spare6,
	c1_PR_spare5,
	c1_PR_spare4,
	c1_PR_spare3,
	c1_PR_spare2,
	c1_PR_spare1
} c1_PR;

/* HandoverCommand */
typedef struct HandoverCommand {
	struct criticalExtensions {
		criticalExtensions_PR present;
		union HandoverCommand__criticalExtensions_u {
			struct c1 {
				c1_PR present;
				union HandoverCommand__criticalExtensions__c1_u {
					HandoverCommand_r8_IEs_t	 handoverCommand_r8;
					NULL_t	 spare7;
					NULL_t	 spare6;
					NULL_t	 spare5;
					NULL_t	 spare4;
					NULL_t	 spare3;
					NULL_t	 spare2;
					NULL_t	 spare1;
				} choice;
				
				/* Context for parsing across buffer boundaries */
				asn_struct_ctx_t _asn_ctx;
			} c1;
			struct criticalExtensionsFuture {
				
				/* Context for parsing across buffer boundaries */
				asn_struct_ctx_t _asn_ctx;
			} criticalExtensionsFuture;
		} choice;
		
		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} criticalExtensions;
	
	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} HandoverCommand_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_HandoverCommand;

#ifdef __cplusplus
}
#endif

#endif	/* _HandoverCommand_H_ */
#include <asn_internal.h>