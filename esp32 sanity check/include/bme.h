#ifndef BME_H
#define BME_H

bool bmeSetup();
bool bmeReadOnce(float &tC, float &rh, float &hPa, float &gas_kohm);

#endif
