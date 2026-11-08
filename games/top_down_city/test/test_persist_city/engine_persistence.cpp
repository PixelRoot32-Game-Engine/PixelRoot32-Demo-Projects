// The engine is installed for persist_test but not built (see platformio.ini),
// so the two persistence translation units the demo links against are compiled
// here, straight from the installed checkout.
#include "persistence/PersistenceService.cpp"
#include "persistence/RecordCodec.cpp"
