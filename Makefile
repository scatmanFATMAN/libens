all:
	cd src/ && $(MAKE)
	cd docs/ && $(MAKE)

clean:
	cd src/ && $(MAKE) clean
	cd docs/ && $(MAKE) clean
	cd test/ && $(MAKE) clean
