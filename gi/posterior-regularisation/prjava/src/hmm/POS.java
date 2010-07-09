package hmm;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.PrintStream;
import java.util.HashMap;

import data.Corpus;

public class POS {

	//public String trainFilename="../posdata/en_train.conll";
	public static String trainFilename="../posdata/small_train.txt";
//	public static String trainFilename="../posdata/en_test.conll";
//	public static String trainFilename="../posdata/trial1.txt";
	
	public static String testFilename="../posdata/en_test.conll";
	//public static String testFilename="../posdata/trial1.txt";
	
	public static String predFilename="../posdata/en_test.predict.conll";
	public static String modelFilename="../posdata/posModel.out";
	public static final int ITER=20;
	public static final int N_STATE=30;
	
	public static void main(String[] args) {
		//POS p=new POS();
		//POS p=new POS(true);
		try {
			PRPOS();
		} catch (FileNotFoundException e) {
			e.printStackTrace();
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	
	public POS() throws FileNotFoundException, IOException{
		Corpus c= new Corpus(trainFilename);
		//size of vocabulary +1 for unknown tokens
		HMM hmm =new HMM(N_STATE, c.getVocabSize()+1,c.getAllData());
		for(int i=0;i<ITER;i++){
			System.out.println("Iter"+i);
			hmm.EM();
			if((i+1)%10==0){
				hmm.writeModel(modelFilename+i);
			}
		}

		hmm.writeModel(modelFilename);
		
		Corpus test=new Corpus(testFilename,c.vocab);
		
		PrintStream ps= io.FileUtil.printstream(new File(predFilename));
		
		int [][]data=test.getAllData();
		for(int i=0;i<data.length;i++){
			int []tag=hmm.viterbi(data[i]);
			String sent[]=test.get(i);
			for(int j=0;j<data[i].length;j++){
				ps.println(sent[j]+"\t"+tag[j]);
			}
			ps.println();
		}
		ps.close();
	}
	
	//POS induction with L1/Linf constraints
	public static void PRPOS() throws FileNotFoundException, IOException{
		Corpus c= new Corpus(trainFilename);
		//size of vocabulary +1 for unknown tokens
		HMM hmm =new HMM(N_STATE, c.getVocabSize()+1,c.getAllData());
		hmm.o=new HMMObjective(hmm);
		for(int i=0;i<ITER;i++){
			System.out.println("Iter: "+i);
			hmm.PREM();
			if((i+1)%10==0){
				hmm.writeModel(modelFilename+i);
			}
		}

		hmm.writeModel(modelFilename);
	}
	
	
	public POS(boolean supervised) throws FileNotFoundException, IOException{
		Corpus c= new Corpus(trainFilename);
		//size of vocabulary +1 for unknown tokens
		HMM hmm =new HMM(c.tagVocab.size() , c.getVocabSize()+1,c.getAllData());
		hmm.train(c.getTagData());

		hmm.writeModel(modelFilename);
		
		Corpus test=new Corpus(testFilename,c.vocab);
		
		HashMap<String, Integer>tagVocab=
			(HashMap<String, Integer>) io.SerializedObjects.readSerializedObject(Corpus.tagalphaFilename);
		String [] tagdict=new String [tagVocab.size()+1];
		for(String key:tagVocab.keySet()){
			tagdict[tagVocab.get(key)]=key;
		}
		tagdict[tagdict.length-1]=Corpus.UNK_TOK;
		
		System.out.println(c.vocab.get("<e>"));
		
		PrintStream ps= io.FileUtil.printstream(new File(predFilename));
		
		int [][]data=test.getAllData();
		for(int i=0;i<data.length;i++){
			int []tag=hmm.viterbi(data[i]);
			String sent[]=test.get(i);
			for(int j=0;j<data[i].length;j++){
				ps.println(sent[j]+"\t"+tagdict[tag[j]]);
			}
			ps.println();
		}
		ps.close();
	}
}
