// Input of the form:
// " the phantom of the opera "    tickets for <PHRASE> tonight ? ||| C=1 ||| seats for <PHRASE> ? </s> ||| C=1 ||| i see <PHRASE> ? </s> ||| C=1
//                      phrase TAB [context]+
// where    context =   phrase ||| C=...        which are separated by |||

// Model parameterised as follows:
// - each phrase, p, is allocated a latent state, t
// - this is used to generate the contexts, c
// - each context is generated using 4 independent multinomials, one for each position LL, L, R, RR

// Training with EM:
// - e-step is estimating q(t) = P(t|p,c) for all x,c
// - m-step is estimating model parameters P(c,t|p) = P(t) P(c|t)
// - PR uses alternate e-step, which first optimizes lambda 
//      min_q KL(q||p) + delta sum_pt max_c E_q[phi_ptc]
//   where
//      q(t|p,c) propto p(t,c|p) exp( -phi_ptc )
//   Then q is used to obtain expectations for vanilla M-step.

// Sexing it up:
// - learn p-specific conditionals P(t|p)
// - or generate phrase internals, e.g., generate edge words from
//   different distribution to central words
// - agreement between phrase->context model and context->phrase model

import java.io.*;
import optimization.gradientBasedMethods.*;
import optimization.gradientBasedMethods.stats.OptimizerStats;
import optimization.gradientBasedMethods.stats.ProjectedOptimizerStats;
import optimization.linesearch.ArmijoLineSearchMinimizationAlongProjectionArc;
import optimization.linesearch.GenericPickFirstStep;
import optimization.linesearch.InterpolationPickFirstStep;
import optimization.linesearch.LineSearchMethod;
import optimization.linesearch.WolfRuleLineSearch;
import optimization.projections.SimplexProjection;
import optimization.stopCriteria.CompositeStopingCriteria;
import optimization.stopCriteria.NormalizedProjectedGradientL2Norm;
import optimization.stopCriteria.NormalizedValueDifference;
import optimization.stopCriteria.ProjectedGradientL2Norm;
import optimization.stopCriteria.StopingCriteria;
import optimization.stopCriteria.ValueDifference;
import optimization.util.MathUtils;

import java.util.*;
import java.util.regex.*;
import gnu.trove.TDoubleArrayList;
import static java.lang.Math.*;

class Lexicon<T>
{
	public int insert(T word)
	{
		Integer i = wordToIndex.get(word);
		if (i == null)
		{
			i = indexToWord.size();
			wordToIndex.put(word, i);
			indexToWord.add(word);
		}
		return i;
	}

	public T lookup(int index)
	{
		return indexToWord.get(index);
	}

	public int size()
	{
		return indexToWord.size();
	}

	private Map<T, Integer> wordToIndex = new HashMap<T, Integer>();
	private List<T> indexToWord = new ArrayList<T>();
}

class PhraseContextModel
{
	// model/optimisation configuration parameters
	int numTags, numEdges;
	boolean posteriorRegularisation = true;
	double constraintScale = 3; // FIXME: make configurable
	
	// copied from L1LMax in depparsing code
	final double c1= 0.0001, c2=0.9, stoppingPrecision = 1e-5, maxStep = 10;
	final int maxZoomEvals = 10, maxExtrapolationIters = 200;
	int maxProjectionIterations = 200;
	int minOccurrencesForProjection = 0;

	// book keeping
	Lexicon<String> tokenLexicon = new Lexicon<String>();
	int numPositions;
	Random rng = new Random();

	// training set; 1 entry for each unique phrase
	PhraseAndContexts training[];

	// model parameters (learnt)
	double emissions[][][]; // position in 0 .. 3 x tag x word Pr(word | tag, position)
	double prior[][]; // phrase x tag Pr(tag | phrase)
	double lambda[]; // edge = (phrase, context) x tag flattened lagrange multipliers

	PhraseContextModel(File infile, int tags) throws IOException
	{
		numTags = tags;
		numEdges = 0;
		readTrainingFromFile(new FileReader(infile));
		assert (training.length > 0);

		// now initialise emissions
		assert (training[0].contexts.length > 0);
		numPositions = training[0].contexts[0].tokens.length;

		emissions = new double[numPositions][numTags][tokenLexicon.size()];
		prior = new double[training.length][numTags];
		if (posteriorRegularisation)
			lambda = new double[numEdges * numTags];

		for (double[][] emissionTW : emissions)
			for (double[] emissionW : emissionTW)
				randomise(emissionW);

		for (double[] priorTag : prior)
			randomise(priorTag);
	}

	void expectationMaximisation(int numIterations)
	{
		double lastLlh = Double.NEGATIVE_INFINITY;

		for (int iteration = 0; iteration < numIterations; ++iteration)
		{
			double emissionsCounts[][][] = new double[numPositions][numTags][tokenLexicon.size()];
			double priorCounts[][] = new double[training.length][numTags];
			
			// E-step
			double llh = 0;
			if (posteriorRegularisation)
			{
				EStepDualObjective objective = new EStepDualObjective();
				
				// copied from x2y2withconstraints
				LineSearchMethod ls = new ArmijoLineSearchMinimizationAlongProjectionArc(new InterpolationPickFirstStep(1));				
				OptimizerStats stats = new OptimizerStats();
				ProjectedGradientDescent optimizer = new ProjectedGradientDescent(ls);
				CompositeStopingCriteria compositeStop = new CompositeStopingCriteria();
				compositeStop.add(new ProjectedGradientL2Norm(0.001));
				compositeStop.add(new ValueDifference(0.001));
				optimizer.setMaxIterations(50);
				boolean succeed = optimizer.optimize(objective,stats,compositeStop);
				
				// copied from depparser l1lmaxobjective
//				ProjectedOptimizerStats stats = new ProjectedOptimizerStats();
//				GenericPickFirstStep pickFirstStep = new GenericPickFirstStep(1);
//				LineSearchMethod linesearch = new WolfRuleLineSearch(pickFirstStep, c1, c2);
//				ProjectedGradientDescent optimizer = new ProjectedGradientDescent(linesearch);
//				optimizer.setMaxIterations(maxProjectionIterations);
//		        StopingCriteria stopGrad = new NormalizedProjectedGradientL2Norm(stoppingPrecision);
//		        StopingCriteria stopValue = new NormalizedValueDifference(stoppingPrecision);
//		        CompositeStopingCriteria stop = new CompositeStopingCriteria();
//		        stop.add(stopGrad);
//		        stop.add(stopValue);
//		        boolean succeed = optimizer.optimize(objective, stats, stop);

				//System.out.println("Ended optimzation Projected Gradient Descent\n" + stats.prettyPrint(1));
				//System.out.println("Solution: " + objective.parameters);
				if (!succeed)
					System.out.println("Failed to optimize");
				//System.out.println("Ended optimization in " + optimizer.getCurrentIteration());				

		        // make sure we update the dual params
				//llh = objective.getValue();
				llh = objective.primal();
				// FIXME: this is the dual not the primal and omits the llh term
				
				for (int i = 0; i < training.length; ++i)
				{
					PhraseAndContexts instance = training[i];
					for (int j = 0; j < instance.contexts.length; ++j)
					{
						Context c = instance.contexts[j];
						for (int t = 0; t < numTags; t++)
						{
							double p = objective.q.get(i).get(j).get(t);
							priorCounts[i][t] += c.count * p;
							for (int k = 0; k < c.tokens.length; ++k)
								emissionsCounts[k][t][c.tokens[k]] += c.count * p;
						}
					}
				}
			}
			else
			{
				for (int i = 0; i < training.length; ++i)
				{
					PhraseAndContexts instance = training[i];
					for (Context ctx : instance.contexts)
					{
						double probs[] = posterior(i, ctx);
						double z = normalise(probs);
						llh += log(z) * ctx.count;
	
						for (int t = 0; t < numTags; ++t)
						{
							priorCounts[i][t] += ctx.count * probs[t];
							for (int j = 0; j < ctx.tokens.length; ++j)
								emissionsCounts[j][t][ctx.tokens[j]] += ctx.count * probs[t];
						}
					}
				}
			}

			// M-step: normalise
			for (double[][] emissionTW : emissionsCounts)
				for (double[] emissionW : emissionTW)
					normalise(emissionW);

			for (double[] priorTag : priorCounts)
				normalise(priorTag);

			emissions = emissionsCounts;
			prior = priorCounts;

			System.out.println("Iteration " + iteration + " llh " + llh);

//			if (llh - lastLlh < 1e-4)
//				break;
//			else
//				lastLlh = llh;
		}
	}

	static double normalise(double probs[])
	{
		double z = 0;
		for (double p : probs)
			z += p;
		for (int i = 0; i < probs.length; ++i)
			probs[i] /= z;
		return z;
	}

	void randomise(double probs[])
	{
		double z = 0;
		for (int i = 0; i < probs.length; ++i)
		{
			probs[i] = 10 + rng.nextDouble();
			z += probs[i];
		}

		for (int i = 0; i < probs.length; ++i)
			probs[i] /= z;
	}

	static int argmax(double probs[])
	{
		double m = Double.NEGATIVE_INFINITY;
		int mi = -1;
		for (int i = 0; i < probs.length; ++i)
		{
			if (probs[i] > m)
			{
				m = probs[i];
				mi = i;
			}
		}
		return mi;
	}

	double[] posterior(int phraseId, Context c) // unnormalised
	{
		double probs[] = new double[numTags];
		for (int t = 0; t < numTags; ++t)
		{
			probs[t] = prior[phraseId][t];
			for (int j = 0; j < c.tokens.length; ++j)
				probs[t] *= emissions[j][t][c.tokens[j]];
		}
		return probs;
	}

	private void readTrainingFromFile(Reader in) throws IOException
	{
		// read in line-by-line
		BufferedReader bin = new BufferedReader(in);
		String line;
		List<PhraseAndContexts> instances = new ArrayList<PhraseAndContexts>();
		Pattern separator = Pattern.compile(" \\|\\|\\| ");

		while ((line = bin.readLine()) != null)
		{
			// split into phrase and contexts
			StringTokenizer st = new StringTokenizer(line, "\t");
			assert (st.hasMoreTokens());
			String phrase = st.nextToken();
			assert (st.hasMoreTokens());
			String rest = st.nextToken();
			assert (!st.hasMoreTokens());

			// process phrase
			st = new StringTokenizer(phrase, " ");
			List<Integer> ptoks = new ArrayList<Integer>();
			while (st.hasMoreTokens())
				ptoks.add(tokenLexicon.insert(st.nextToken()));

			// process contexts
			ArrayList<Context> contexts = new ArrayList<Context>();
			String[] parts = separator.split(rest);
			assert (parts.length % 2 == 0);
			for (int i = 0; i < parts.length; i += 2)
			{
				// process pairs of strings - context and count
				ArrayList<Integer> ctx = new ArrayList<Integer>();
				String ctxString = parts[i];
				String countString = parts[i + 1];
				StringTokenizer ctxStrtok = new StringTokenizer(ctxString, " ");
				while (ctxStrtok.hasMoreTokens())
				{
					String token = ctxStrtok.nextToken();
					if (!token.equals("<PHRASE>"))
						ctx.add(tokenLexicon.insert(token));
				}

				assert (countString.startsWith("C="));
				Context c = new Context();
				c.count = Integer.parseInt(countString.substring(2).trim());
				// damn unboxing doesn't work with toArray
				c.tokens = new int[ctx.size()];
				for (int k = 0; k < ctx.size(); ++k)
					c.tokens[k] = ctx.get(k);
				contexts.add(c);

				numEdges += 1;
			}

			// package up
			PhraseAndContexts instance = new PhraseAndContexts();
			// damn unboxing doesn't work with toArray
			instance.phraseTokens = new int[ptoks.size()];
			for (int k = 0; k < ptoks.size(); ++k)
				instance.phraseTokens[k] = ptoks.get(k);
			instance.contexts = contexts.toArray(new Context[] {});
			instances.add(instance);
		}

		training = instances.toArray(new PhraseAndContexts[] {});

		System.out.println("Read in " + training.length + " phrases and " + numEdges + " edges");
	}

	void displayPosterior()
	{
		for (int i = 0; i < training.length; ++i)
		{
			PhraseAndContexts instance = training[i];
			for (Context ctx : instance.contexts)
			{
				double probs[] = posterior(i, ctx);
				normalise(probs);

				// emit phrase
				for (int t : instance.phraseTokens)
					System.out.print(tokenLexicon.lookup(t) + " ");
				System.out.print("\t");
				for (int c : ctx.tokens)
					System.out.print(tokenLexicon.lookup(c) + " ");
				System.out.print("||| C=" + ctx.count + " |||");

				int t = argmax(probs);
				System.out.print(" " + t + " ||| " + probs[t]);
				// for (int t = 0; t < numTags; ++t)
				// System.out.print(" " + probs[t]);
				System.out.println();
			}
		}
	}

	class PhraseAndContexts
	{
		int phraseTokens[];
		Context contexts[];
	}

	class Context
	{
		int count;
		int[] tokens;
	}

	public static void main(String[] args)
	{
		assert (args.length >= 2);
		try
		{
			PhraseContextModel model = new PhraseContextModel(new File(args[0]), Integer.parseInt(args[1]));
			model.expectationMaximisation(Integer.parseInt(args[2]));
			model.displayPosterior();
		} 
		catch (IOException e)
		{
			System.out.println("Failed to read input file: " + args[0]);
			e.printStackTrace();
		}
	}

	class EStepDualObjective extends ProjectedObjective
	{
		List<List<TDoubleArrayList>> conditionals; // phrase id x context # x tag - precomputed
		List<List<TDoubleArrayList>> q; // ditto, but including exp(-lambda) terms
		double objective = 0; // log(z)
		// Objective.gradient = d log(z) / d lambda = E_q[phi]
		double llh = 0;

		public EStepDualObjective()
		{
			super();
			// compute conditionals p(context, tag | phrase) for all training instances
			conditionals = new ArrayList<List<TDoubleArrayList>>(training.length);
			q = new ArrayList<List<TDoubleArrayList>>(training.length);
			for (int i = 0; i < training.length; ++i)
			{
				PhraseAndContexts instance = training[i];
				conditionals.add(new ArrayList<TDoubleArrayList>(instance.contexts.length));
				q.add(new ArrayList<TDoubleArrayList>(instance.contexts.length));

				for (int j = 0; j < instance.contexts.length; ++j)
				{
					Context c = instance.contexts[j];
					double probs[] = posterior(i, c);
					double z = normalise(probs);
					llh += log(z) * c.count;
					conditionals.get(i).add(new TDoubleArrayList(probs));
					q.get(i).add(new TDoubleArrayList(probs));
				}
			}
			
			gradient = new double[numEdges*numTags];
			setInitialParameters(lambda);
		}

		@Override
		public double[] projectPoint(double[] point)
		{
			SimplexProjection p = new SimplexProjection(constraintScale);

			double[] newPoint = point.clone();
			int edgeIndex = 0;
			for (int i = 0; i < training.length; ++i)
			{
				PhraseAndContexts instance = training[i];				

				for (int t = 0; t < numTags; t++)
				{
					double[] subPoint = new double[instance.contexts.length];
					for (int j = 0; j < instance.contexts.length; ++j)
						subPoint[j] = point[edgeIndex+j*numTags+t];

					p.project(subPoint);
					for (int j = 0; j < instance.contexts.length; ++j)
						newPoint[edgeIndex+j*numTags+t] = subPoint[j];
				}
				
				edgeIndex += instance.contexts.length * numTags;
			}
			//System.out.println("Project point: " + Arrays.toString(point) 
			//		+ " => " + Arrays.toString(newPoint));
			return newPoint;
		}

		@Override
		public void setParameters(double[] params)
		{
			super.setParameters(params);
			computeObjectiveAndGradient();
		}

		@Override
		public double[] getGradient()
		{
			return gradient;
		}

		@Override
		public double getValue()
		{
			return objective;
		}

		public void computeObjectiveAndGradient()
		{
			int edgeIndex = 0;
			objective = 0;
			Arrays.fill(gradient, 0);
			for (int i = 0; i < training.length; ++i)
			{
				PhraseAndContexts instance = training[i];
				
				for (int j = 0; j < instance.contexts.length; ++j)
				{
					Context c = instance.contexts[j];
					
					double z = 0;
					for (int t = 0; t < numTags; t++)
					{
						double v = conditionals.get(i).get(j).get(t) * exp(-parameters[edgeIndex+t]);
						q.get(i).get(j).set(t, v);
						z += v;
					}
					objective = log(z) * c.count;

					for (int t = 0; t < numTags; t++)
					{
						double v = q.get(i).get(j).get(t) / z; 
						q.get(i).get(j).set(t, v);
						gradient[edgeIndex+t] -= c.count * v;
					}
					
					edgeIndex += numTags;
				}
			}			
//			System.out.println("computeObjectiveAndGradient logz=" + objective);
//			System.out.println("gradient=" + Arrays.toString(gradient));
		}

		public String toString()
		{
			StringBuilder sb = new StringBuilder();
			sb.append(getClass().getCanonicalName()).append(" with ");
			sb.append(parameters.length).append(" parameters and ");
			sb.append(training.length * numTags).append(" constraints");
			return sb.toString();
		}
				
		double primal()
		{
			// primal = llh + KL(q||p) + scale * sum_pt max_c E_q[phi_pct]
			// kl = sum_Y q(Y) log q(Y) / p(Y|X)
			//    = sum_Y q(Y) { -lambda . phi(Y) - log Z }
			//    = -log Z - lambda . E_q[phi]
			
			double kl = -objective - MathUtils.dotProduct(parameters, gradient);
			double l1lmax = 0;
			for (int i = 0; i < training.length; ++i)
			{
				PhraseAndContexts instance = training[i];				
				for (int t = 0; t < numTags; t++)
				{
					double lmax = Double.NEGATIVE_INFINITY;
					for (int j = 0; j < instance.contexts.length; ++j)
						lmax = max(lmax, q.get(i).get(j).get(t));
					l1lmax += lmax;
				}
			}

			return llh + kl + constraintScale * l1lmax;
		}
	}
}
