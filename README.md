# Results
See my results in results/
# Replication
## 1. Edit Scripts
Edit `DEVICE=` in the scripts to use files on your hard drive and solid state drive respectively
## Step 2. Run the benchmarks
This can be done in parallel in two terminal windows. 
You should see results start to fill up in `hdd_benchmark_results` and `ssd_benchmark_results`. The scripts will run `gcc` and `dd` to compile the program and allocate the test file(s) for setup.
```bash
./benchmark-hdd.sh
./benchmark-ssd.sh
```
## Step 3. Generate graphs
After these scripts run you should, to generate the graphs run the following
```bash
python -m venv venv
source ./venv/bin/activate
pip install -r requirements.txt
python generate_graphs.py
```
