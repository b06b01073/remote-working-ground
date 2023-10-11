all:
	g++ ./np_multi_proc.cpp -o ./np_multi_proc
	g++ ./np_single_proc.cpp -o ./np_single_proc
	g++ ./np_simple.cpp -o ./np_simple
clean:
	rm ./*.txt