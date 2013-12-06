clean:
	rm sphinx3_livesegment

sphinx3_livesegment: sphinx3_livesegment.c
	gcc sphinx3_livesegment.c -I$(SPHINXBASE_PATH)/include -I$(SPHINX3_PATH)/include -lsphinxad -ls3decoder -lm -o sphinx3_livesegment
