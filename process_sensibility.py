import os
import itertools
import subprocess
import shutil
import sys
import configparser
import math
import json
import statistics
import numpy as np
import matplotlib.pyplot as plt
from ParametricConfigUtils import *

# I assume that this file is in the root of the repo
root = os.path.dirname(os.path.abspath(__file__))
root += '/'

#   Parameters ranges
ranges = [
    ('MinIndividualsPerSpecies', [5, 10, 50, 100]),
    ('MinSpeciesAge', [1, 10, 50, 100]),
    ('ProgressMetricsIndividuals', [1, 5, 20, 40]),
    ('ProgressMetricsFalloff', [0.001, 0.025, 0.1, 0.9]),
    ('ProgressThreshold', [0.0001, 0.005, 0.1, 0.9]),
    ('SpeciesStagnancyChances', [1, 10, 50, 100]),
    ('MorphologyTries', [1, 10, 50, 100]),
    ('parametermutationprob', [0.01, 0.1, 0.2, 0.5]),
    ('parameterdestructivemutationprob', [0.01, 0.1, 0.2, 0.5]),
    ('parametermutationspread', [0.001, 0.025, 0.1, 0.9]),
]

flat_ranges = []
for name, values in ranges:
    for v in values:
        flat_ranges.append((name,v))

eval_repeats = 50

# The idea of the sensibility test is to see if the parameter affects or not the results
for name, values in ranges:
    species_nums = []
    results = {}
    for v in values:
        avg_num_species = 0
        tmp_results = {}
        for eval_idx in range(eval_repeats):
            for world_idx in range(5):
                species = parse_file('parametric_config/sensibility/results/{0}_{1}_{2}_{3}.txt'.format(name,v,eval_idx,world_idx))
                for tag,avg_fit in species:
                    try:
                        tmp_results[str(tag)].append(avg_fit)
                    except:
                        tmp_results[str(tag)] = [avg_fit]
                avg_num_species += len(species)
        for key,repeats_vals in tmp_results.items():
            try:
                results[key].append(statistics.mean(repeats_vals))
            except:
                results[key] = [statistics.mean(repeats_vals)]
        avg_num_species /= eval_repeats
        species_nums.append(avg_num_species)
    
    cv_vals = []
    for _, fit_vals in results.items():
        if statistics.mean(fit_vals) <= 0:
            continue
        fit_cv = statistics.pstdev(fit_vals) / statistics.mean(fit_vals)
        cv_vals.append(fit_cv)

    snum_cv = statistics.pstdev(species_nums) / statistics.mean(species_nums)
    avg_fit_cv = statistics.mean(cv_vals)
    print("{0} & {1:.2f} & {2:.2f} & {3:.2f}\\\\".format(name, snum_cv, avg_fit_cv, 0.5*(snum_cv + avg_fit_cv)))

