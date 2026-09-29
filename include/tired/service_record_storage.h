#ifndef TIRED_SERVICE_RECORD_STORAGE_H
#define TIRED_SERVICE_RECORD_STORAGE_H
#include "tired/directory.h"
#include "tired/service_record.h"
/* Read UUID.json from an already trusted records directory; caller associates the
 * directory with the selected layout. Requires private owner storage and matching
 * embedded UUID, scope, owner and derived unit/environment paths. No creation,
 * repair or ownership authorization. Atomic owned output, snapshot only. */
bool tired_service_record_read(TiredDirectory *directory, const TiredLayout *layout,
                               const char *uuid, TiredServiceRecord *output, TiredError *error);
/* Open the selected layout's records directory without creating it and read the
 * canonical UUID filename. Missing paths remain NOT_FOUND, not successful empty
 * records. Permission/trust failures remain distinct. */
bool tired_service_record_load(const TiredLayout *layout, const char *uuid,
                               TiredServiceRecord *output, TiredError *error);
#endif
