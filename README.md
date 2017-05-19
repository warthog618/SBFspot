# SBFspot
Yet another tool to read power production of SMA solar inverters.

This is a branch of SBFspot intended to improve performance of the SBFspotUploadDaemon when using SQLite.
Motivated by poor performance of the offical SBFspotUploadDaemon with large databases (I have >2 years of data) running on a Raspberry Pi.
The offical daemon was taking a large amount of CPU time (>45sec per upload on a Raspberry Pi 2) to generate the update from the database - even for nothing to do.
The updated version generates the upload in a few seconds for the most common case - one entry to upload.

I initially thought the problem could be solved by optimising the database views, but after investigating for a while found the view approach to be flawed - it performs transforms on the data THEN applies filtering to generate the upload.
This scales badly as the view (the transform) has to be regenerated every time a query is made, and this takes longer and longer as the database grows.
The updated version reverses this approach - filtering the data first then performing the transform to generate the upload.
This bypasses the database views totally to get data directly from the SQL tables.
I think this approach is valid and equivalent, and it has been working fine for me for several weeks now, but I'm only testing one specific installation so YMMV.
