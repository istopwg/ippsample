:: Script to copy DLLs to build directory
::
:: Usage:
::
::   .\copydlls.bat x64\{Debug|Release}
::

:: Copy dependent DLLs to the named build directory
echo Copying DLLs
copy ..\libcups\pdfio\packages\libpng_native.redist.1.6.30\build\native\bin\x64\Debug\*.dll %1
copy ..\libcups\pdfio\packages\libpng_native.redist.1.6.30\build\native\bin\x64\Release\*.dll %1

copy ..\libcups\vcnet\packages\libressl_native.redist.4.0.0\build\native\bin\x64\Debug\*.dll %1
copy ..\libcups\vcnet\packages\libressl_native.redist.4.0.0\build\native\bin\x64\Release\*.dll %1

copy ..\libcups\vcnet\packages\zlib_native.redist.1.2.11\build\native\bin\x64\Debug\*.dll %1
copy ..\libcups\vcnet\packages\zlib_native.redist.1.2.11\build\native\bin\x64\Release\*.dll %1
